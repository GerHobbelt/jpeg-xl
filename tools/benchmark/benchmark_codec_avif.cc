// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
#include "tools/benchmark/benchmark_codec_avif.h"

#ifdef BENCHMARK_AVIF

#include <avif/avif.h>
#include <jxl/cms.h>
#include <jxl/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lib/extras/codec_in_out.h"
#include "lib/extras/packed_image_convert.h"
#include "lib/extras/time.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/data_parallel.h"
#include "lib/jxl/base/span.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/color_encoding_internal.h"
#include "lib/jxl/dec_external_image.h"
#include "lib/jxl/enc_external_image.h"
#include "lib/jxl/image_bundle.h"
#include "lib/jxl/image_metadata.h"
#include "tools/benchmark/benchmark_args.h"
#include "tools/benchmark/benchmark_codec.h"
#include "tools/cmdline.h"
#include "tools/no_memory_manager.h"
#include "tools/speed_stats.h"
#include "tools/thread_pool_internal.h"

#define JXL_RETURN_IF_AVIF_ERROR(result)                                       \
  do {                                                                         \
    avifResult jxl_return_if_avif_error_result = (result);                     \
    if (jxl_return_if_avif_error_result != AVIF_RESULT_OK) {                   \
      return JXL_FAILURE("libavif error: %s",                                  \
                         avifResultToString(jxl_return_if_avif_error_result)); \
    }                                                                          \
  } while (false)

namespace jpegxl {
namespace tools {

using ::jxl::Bytes;
using ::jxl::CodecInOut;
using ::jxl::IccBytes;
using ::jxl::ImageBundle;
using ::jxl::Primaries;
using ::jxl::Span;
using ::jxl::ThreadPool;
using ::jxl::TransferFunction;
using ::jxl::WhitePoint;

namespace {

size_t GetNumThreads(ThreadPool* pool) {
  size_t result = 0;
  const auto count_threads = [&](const size_t num_threads) -> Status {
    result = num_threads;
    return true;
  };
  const auto no_op = [&](const uint32_t /*task*/, size_t /*thread*/) -> Status {
    return true;
  };
  (void)jxl::RunOnPool(pool, 0, 1, count_threads, no_op, "Compress");
  return result;
}

struct AvifArgs {
  avifPixelFormat chroma_subsampling = AVIF_PIXEL_FORMAT_YUV444;
};

AvifArgs* const avifargs = new AvifArgs;

bool ParseChromaSubsampling(const char* arg, avifPixelFormat* subsampling) {
  if (strcmp(arg, "444") == 0) {
    *subsampling = AVIF_PIXEL_FORMAT_YUV444;
    return true;
  }
  if (strcmp(arg, "422") == 0) {
    *subsampling = AVIF_PIXEL_FORMAT_YUV422;
    return true;
  }
  if (strcmp(arg, "420") == 0) {
    *subsampling = AVIF_PIXEL_FORMAT_YUV420;
    return true;
  }
  if (strcmp(arg, "400") == 0) {
    *subsampling = AVIF_PIXEL_FORMAT_YUV400;
    return true;
  }
  return false;
}

Status SetUpAvifColor(const ColorEncoding& color, bool rgb,
                      avifImage* const image) {
  bool need_icc = (color.GetWhitePointType() != WhitePoint::kD65);

  image->matrixCoefficients =
      rgb ? AVIF_MATRIX_COEFFICIENTS_IDENTITY : AVIF_MATRIX_COEFFICIENTS_BT709;
  if (!color.HasPrimaries()) {
    need_icc = true;
  } else {
    switch (color.GetPrimariesType()) {
      case Primaries::kSRGB:
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
        break;
      case Primaries::k2100:
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT2020;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;
        break;
      default:
        need_icc = true;
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_UNKNOWN;
        break;
    }
  }

  switch (color.Tf().GetTransferFunction()) {
    case TransferFunction::kSRGB:
      image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
      break;
    case TransferFunction::kLinear:
      image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_LINEAR;
      break;
    case TransferFunction::kPQ:
      image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
      break;
    case TransferFunction::kHLG:
      image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_HLG;
      break;
    default:
      need_icc = true;
      image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN;
      break;
  }

  if (need_icc) {
#if AVIF_VERSION_MAJOR < 1
    avifImageSetProfileICC(image, color.ICC().data(), color.ICC().size());
#else
    JXL_RETURN_IF_AVIF_ERROR(
        avifImageSetProfileICC(image, color.ICC().data(), color.ICC().size()));
#endif
  }
  return true;
}

Status ReadAvifColor(const avifImage* const image, ColorEncoding* const color) {
  if (image->icc.size != 0) {
    IccBytes icc;
    icc.assign(image->icc.data, image->icc.data + image->icc.size);
    return color->SetICC(std::move(icc), JxlGetDefaultCms());
  }

  JXL_RETURN_IF_ERROR(color->SetWhitePointType(WhitePoint::kD65));
  switch (image->colorPrimaries) {
    case AVIF_COLOR_PRIMARIES_BT709:
      JXL_RETURN_IF_ERROR(color->SetPrimariesType(Primaries::kSRGB));
      break;
    case AVIF_COLOR_PRIMARIES_BT2020:
      JXL_RETURN_IF_ERROR(color->SetPrimariesType(Primaries::k2100));
      break;
    default:
      return JXL_FAILURE("unsupported avif primaries");
  }
  jxl::cms::CustomTransferFunction& tf = color->Tf();
  switch (image->transferCharacteristics) {
    case AVIF_TRANSFER_CHARACTERISTICS_BT470M:
      JXL_RETURN_IF_ERROR(tf.SetGamma(2.2));
      break;
    case AVIF_TRANSFER_CHARACTERISTICS_BT470BG:
      JXL_RETURN_IF_ERROR(tf.SetGamma(2.8));
      break;
    case AVIF_TRANSFER_CHARACTERISTICS_LINEAR:
      tf.SetTransferFunction(TransferFunction::kLinear);
      break;
    case AVIF_TRANSFER_CHARACTERISTICS_SRGB:
      tf.SetTransferFunction(TransferFunction::kSRGB);
      break;
    case AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084:
      tf.SetTransferFunction(TransferFunction::kPQ);
      break;
    case AVIF_TRANSFER_CHARACTERISTICS_HLG:
      tf.SetTransferFunction(TransferFunction::kHLG);
      break;
    default:
      return JXL_FAILURE("unsupported avif TRC");
  }
  return color->CreateICC();
}

}  // namespace

Status AddCommandLineOptionsAvifCodec(BenchmarkArgs* args) {
  args->cmdline.AddOptionValue(
      '\0', "avif_chroma_subsampling", "444/422/420/400",
      "default AVIF chroma subsampling (default: 444).",
      &avifargs->chroma_subsampling, &ParseChromaSubsampling);
  return true;
}

class AvifCodec : public ImageCodec {
 public:
  explicit AvifCodec(const BenchmarkArgs& args) : ImageCodec(args) {
    chroma_subsampling_ = avifargs->chroma_subsampling;
  }

  Status ParseParam(const std::string& param) override {
    if (param.compare(0, 3, "yuv") == 0) {
      if (param.size() != 6) return false;
      return ParseChromaSubsampling(param.c_str() + 3, &chroma_subsampling_);
    }
    if (param == "rgb") {
      rgb_ = true;
      return true;
    }
    if (param.compare(0, 10, "log2_cols=") == 0) {
      log2_cols = strtol(param.c_str() + 10, nullptr, 10);
      return true;
    }
    if (param.compare(0, 10, "log2_rows=") == 0) {
      log2_rows = strtol(param.c_str() + 10, nullptr, 10);
      return true;
    }
    if (param[0] == 's') {
      speed_ = strtol(param.c_str() + 1, nullptr, 10);
      return true;
    }
    if (param == "aomenc") {
      encoder_ = AVIF_CODEC_CHOICE_AOM;
      return true;
    }
    if (param == "aomdec") {
      decoder_ = AVIF_CODEC_CHOICE_AOM;
      return true;
    }
    if (param == "aom") {
      encoder_ = AVIF_CODEC_CHOICE_AOM;
      decoder_ = AVIF_CODEC_CHOICE_AOM;
      return true;
    }
    if (param == "rav1e") {
      encoder_ = AVIF_CODEC_CHOICE_RAV1E;
      return true;
    }
    if (param == "dav1d") {
      decoder_ = AVIF_CODEC_CHOICE_DAV1D;
      return true;
    }
    if (param.compare(0, 2, "a=") == 0) {
      std::string subparam = param.substr(2);
      size_t pos = subparam.find('=');
      if (pos == std::string::npos) {
        codec_specific_options_.emplace_back(subparam, "");
      } else {
        std::string key = subparam.substr(0, pos);
        std::string value = subparam.substr(pos + 1);
        codec_specific_options_.emplace_back(key, value);
      }
      return true;
    }
    return ImageCodec::ParseParam(param);
  }

  Status Compress(const std::string& filename, const PackedPixelFile& ppf,
                  ThreadPool* pool, std::vector<uint8_t>* compressed,
                  jpegxl::tools::SpeedStats* speed_stats) override {
    auto io = jxl::make_unique<CodecInOut>(jpegxl::tools::NoMemoryManager());
    JXL_RETURN_IF_ERROR(
        jxl::extras::ConvertPackedPixelFileToCodecInOut(ppf, pool, io.get()));
    return Compress(filename, io.get(), pool, compressed, speed_stats);
  }

  Status Compress(const std::string& filename, const CodecInOut* io,
                  ThreadPool* pool, std::vector<uint8_t>* compressed,
                  SpeedStats* speed_stats) {
    double elapsed_convert_image = 0;
    size_t max_threads = GetNumThreads(pool);
    const double start = jxl::Now();
    {
      const auto depth =
          std::min<int>(16, io->metadata.m.bit_depth.bits_per_sample);
      std::unique_ptr<avifEncoder, void (*)(avifEncoder*)> encoder(
          avifEncoderCreate(), &avifEncoderDestroy);
      encoder->codecChoice = encoder_;
      // TODO(sboukortt): configure this separately.
      encoder->minQuantizer = 0;
      encoder->maxQuantizer = 63;
#if AVIF_VERSION >= 1000300
      encoder->quality = q_target_;
      encoder->qualityAlpha = q_target_;
#endif
      encoder->tileColsLog2 = log2_cols;
      encoder->tileRowsLog2 = log2_rows;
      encoder->speed = speed_;
      encoder->maxThreads = max_threads;
      for (const auto& opts : codec_specific_options_) {
#if AVIF_VERSION_MAJOR >= 1
        JXL_RETURN_IF_AVIF_ERROR(avifEncoderSetCodecSpecificOption(
            encoder.get(), opts.first.c_str(), opts.second.c_str()));
#else
        (void)avifEncoderSetCodecSpecificOption(
            encoder.get(), opts.first.c_str(), opts.second.c_str());
#endif
      }
      avifAddImageFlags add_image_flags = AVIF_ADD_IMAGE_FLAG_SINGLE;
      if (io->metadata.m.have_animation) {
        encoder->timescale = std::lround(
            static_cast<float>(io->metadata.m.animation.tps_numerator) /
            io->metadata.m.animation.tps_denominator);
        add_image_flags = AVIF_ADD_IMAGE_FLAG_NONE;
      }
      for (const ImageBundle& ib : io->frames) {
        std::unique_ptr<avifImage, void (*)(avifImage*)> image(
            avifImageCreate(ib.xsize(), ib.ysize(), depth, chroma_subsampling_),
            &avifImageDestroy);
        image->width = ib.xsize();
        image->height = ib.ysize();
        image->depth = depth;
        JXL_RETURN_IF_ERROR(SetUpAvifColor(ib.c_current(), rgb_, image.get()));
        std::unique_ptr<avifRWData, void (*)(avifRWData*)> icc_freer(
            &image->icc, &avifRWDataFree);
        avifRGBImage rgb_image;
        avifRGBImageSetDefaults(&rgb_image, image.get());
        rgb_image.format =
            ib.HasAlpha() ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
#if AVIF_VERSION_MAJOR < 1
        avifRGBImageAllocatePixels(&rgb_image);
#else
        JXL_RETURN_IF_AVIF_ERROR(avifRGBImageAllocatePixels(&rgb_image));
#endif
        std::unique_ptr<avifRGBImage, void (*)(avifRGBImage*)> pixels_freer(
            &rgb_image, &avifRGBImageFreePixels);
        const double start_convert_image = jxl::Now();
        JXL_RETURN_IF_ERROR(ConvertToExternal(
            ib, depth, /*float_out=*/false,
            /*num_channels=*/ib.HasAlpha() ? 4 : 3, JXL_NATIVE_ENDIAN,
            /*stride_out=*/rgb_image.rowBytes, pool, rgb_image.pixels,
            rgb_image.rowBytes * rgb_image.height,
            /*out_callback=*/{}, jxl::Orientation::kIdentity));
        const double end_convert_image = jxl::Now();
        elapsed_convert_image += end_convert_image - start_convert_image;
        JXL_RETURN_IF_AVIF_ERROR(avifImageRGBToYUV(image.get(), &rgb_image));
        JXL_RETURN_IF_AVIF_ERROR(avifEncoderAddImage(
            encoder.get(), image.get(), ib.duration, add_image_flags));
      }
      avifRWData buffer = AVIF_DATA_EMPTY;
      JXL_RETURN_IF_AVIF_ERROR(avifEncoderFinish(encoder.get(), &buffer));
      compressed->assign(buffer.data, buffer.data + buffer.size);
      avifRWDataFree(&buffer);
    }
    const double end = jxl::Now();
    speed_stats->NotifyElapsed(end - start - elapsed_convert_image);
    return true;
  }

  Status Decompress(const std::string& filename,
                    const Span<const uint8_t> compressed, ThreadPool* pool,
                    PackedPixelFile* ppf,
                    jpegxl::tools::SpeedStats* speed_stats) override {
    auto io = jxl::make_unique<CodecInOut>(jpegxl::tools::NoMemoryManager());
    JXL_RETURN_IF_ERROR(
        Decompress(filename, compressed, pool, io.get(), speed_stats));
    JxlPixelFormat format{0, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    return jxl::extras::ConvertCodecInOutToPackedPixelFile(
        *io, format, io->Main().c_current(), pool, ppf);
  };

  Status Decompress(const std::string& filename,
                    const Span<const uint8_t> compressed, ThreadPool* pool,
                    CodecInOut* io, SpeedStats* speed_stats) {
    io->frames.clear();
    size_t max_threads = GetNumThreads(pool);
    double elapsed_convert_image = 0;
    const double start = jxl::Now();
    {
      std::unique_ptr<avifDecoder, void (*)(avifDecoder*)> decoder(
          avifDecoderCreate(), &avifDecoderDestroy);
      decoder->codecChoice = decoder_;
      decoder->maxThreads = max_threads;
      JXL_RETURN_IF_AVIF_ERROR(avifDecoderSetIOMemory(
          decoder.get(), compressed.data(), compressed.size()));
      JXL_RETURN_IF_AVIF_ERROR(avifDecoderParse(decoder.get()));
      const bool has_alpha = FROM_JXL_BOOL(decoder->alphaPresent);
      io->metadata.m.have_animation = decoder->imageCount > 1;
      io->metadata.m.animation.tps_numerator = decoder->timescale;
      io->metadata.m.animation.tps_denominator = 1;
      io->metadata.m.SetUintSamples(decoder->image->depth);
      JXL_RETURN_IF_ERROR(
          io->SetSize(decoder->image->width, decoder->image->height));
      avifResult next_image;
      while ((next_image = avifDecoderNextImage(decoder.get())) ==
             AVIF_RESULT_OK) {
        ColorEncoding color;
        JXL_RETURN_IF_ERROR(ReadAvifColor(decoder->image, &color));
        avifRGBImage rgb_image;
        avifRGBImageSetDefaults(&rgb_image, decoder->image);
        rgb_image.format =
            has_alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
#if AVIF_VERSION_MAJOR < 1
        avifRGBImageAllocatePixels(&rgb_image);
#else
        JXL_RETURN_IF_AVIF_ERROR(avifRGBImageAllocatePixels(&rgb_image));
#endif
        std::unique_ptr<avifRGBImage, void (*)(avifRGBImage*)> pixels_freer(
            &rgb_image, &avifRGBImageFreePixels);
        JXL_RETURN_IF_AVIF_ERROR(avifImageYUVToRGB(decoder->image, &rgb_image));
        const double start_convert_image = jxl::Now();
        {
          JxlPixelFormat format = {
              (has_alpha ? 4u : 3u),
              (rgb_image.depth <= 8 ? JXL_TYPE_UINT8 : JXL_TYPE_UINT16),
              JXL_NATIVE_ENDIAN, 0};
          ImageBundle ib(jpegxl::tools::NoMemoryManager(), &io->metadata.m);
          JXL_RETURN_IF_ERROR(ConvertFromExternal(
              Bytes(rgb_image.pixels, rgb_image.height * rgb_image.rowBytes),
              rgb_image.width, rgb_image.height, color, rgb_image.depth, format,
              pool, &ib));
          io->frames.push_back(std::move(ib));
        }
        const double end_convert_image = jxl::Now();
        elapsed_convert_image += end_convert_image - start_convert_image;
      }
      if (next_image != AVIF_RESULT_NO_IMAGES_REMAINING) {
        JXL_RETURN_IF_AVIF_ERROR(next_image);
      }
    }
    const double end = jxl::Now();
    speed_stats->NotifyElapsed(end - start - elapsed_convert_image);
    return true;
  }

 protected:
  avifPixelFormat chroma_subsampling_;
  avifCodecChoice encoder_ = AVIF_CODEC_CHOICE_AUTO;
  avifCodecChoice decoder_ = AVIF_CODEC_CHOICE_AUTO;
  bool rgb_ = false;
  int speed_ = AVIF_SPEED_DEFAULT;
  int log2_cols = 0;
  int log2_rows = 0;
  std::vector<std::pair<std::string, std::string>> codec_specific_options_;
};

ImageCodec* CreateNewAvifCodec(const BenchmarkArgs& args) {
  return new AvifCodec(args);
}

}  // namespace tools
}  // namespace jpegxl
#endif
