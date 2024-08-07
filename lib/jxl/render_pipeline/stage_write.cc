// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/render_pipeline/stage_write.h"

#include "lib/jxl/alpha.h"
#include "lib/jxl/common.h"
#include "lib/jxl/dec_cache.h"
#include "lib/jxl/image_bundle.h"
#include "lib/jxl/sanitizers.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "lib/jxl/render_pipeline/stage_write.cc"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace jxl {
namespace HWY_NAMESPACE {

// These templates are not found via ADL.
using hwy::HWY_NAMESPACE::Clamp;
using hwy::HWY_NAMESPACE::Mul;
using hwy::HWY_NAMESPACE::NearestInt;
using hwy::HWY_NAMESPACE::Rebind;
using hwy::HWY_NAMESPACE::U8FromU32;

template <typename D, typename V>
void StoreRGBA(D d, V r, V g, V b, V a, bool alpha, size_t n, size_t extra,
               uint8_t* buf) {
#if HWY_TARGET == HWY_SCALAR
  buf[0] = r.raw;
  buf[1] = g.raw;
  buf[2] = b.raw;
  if (alpha) {
    buf[3] = a.raw;
  }
#elif HWY_TARGET == HWY_NEON
  if (alpha) {
    uint8x8x4_t data = {r.raw, g.raw, b.raw, a.raw};
    if (extra >= 8) {
      vst4_u8(buf, data);
    } else {
      uint8_t tmp[8 * 4];
      vst4_u8(tmp, data);
      memcpy(buf, tmp, n * 4);
    }
  } else {
    uint8x8x3_t data = {r.raw, g.raw, b.raw};
    if (extra >= 8) {
      vst3_u8(buf, data);
    } else {
      uint8_t tmp[8 * 3];
      vst3_u8(tmp, data);
      memcpy(buf, tmp, n * 3);
    }
  }
#else
  // TODO(veluca): implement this for x86.
  size_t mul = alpha ? 4 : 3;
  HWY_ALIGN uint8_t bytes[16];
  StoreU(r, d, bytes);
  for (size_t i = 0; i < n; i++) {
    buf[mul * i] = bytes[i];
  }
  StoreU(g, d, bytes);
  for (size_t i = 0; i < n; i++) {
    buf[mul * i + 1] = bytes[i];
  }
  StoreU(b, d, bytes);
  for (size_t i = 0; i < n; i++) {
    buf[mul * i + 2] = bytes[i];
  }
  if (alpha) {
    StoreU(a, d, bytes);
    for (size_t i = 0; i < n; i++) {
      buf[4 * i + 3] = bytes[i];
    }
  }
#endif
}

class WriteToU8Stage : public RenderPipelineStage {
 public:
  WriteToU8Stage(uint8_t* rgb, size_t stride, size_t height, bool rgba,
                 bool has_alpha, size_t alpha_c)
      : RenderPipelineStage(RenderPipelineStage::Settings()),
        rgb_(rgb),
        stride_(stride),
        height_(height),
        rgba_(rgba),
        has_alpha_(has_alpha),
        alpha_c_(alpha_c) {}

  void ProcessRow(const RowInfo& input_rows, const RowInfo& output_rows,
                  size_t xextra, size_t xsize, size_t xpos, size_t ypos,
                  size_t thread_id) const final {
    if (ypos >= height_) return;
    JXL_DASSERT(xextra == 0);
    size_t bytes = rgba_ ? 4 : 3;
    const float* JXL_RESTRICT row_in_r = GetInputRow(input_rows, 0, 0);
    const float* JXL_RESTRICT row_in_g = GetInputRow(input_rows, 1, 0);
    const float* JXL_RESTRICT row_in_b = GetInputRow(input_rows, 2, 0);
    const float* JXL_RESTRICT row_in_a =
        has_alpha_ ? GetInputRow(input_rows, alpha_c_, 0) : nullptr;
    size_t base_ptr = ypos * stride_ + bytes * (xpos - xextra);
    using D = HWY_CAPPED(float, 4);
    const D d;
    D::Rebind<uint32_t> du;
    auto zero = Zero(d);
    auto one = Set(d, 1.0f);
    auto mul = Set(d, 255.0f);

    ssize_t x1 = RoundUpTo(xsize, Lanes(d));

    msan::UnpoisonMemory(row_in_r + xsize, sizeof(float) * (x1 - xsize));
    msan::UnpoisonMemory(row_in_g + xsize, sizeof(float) * (x1 - xsize));
    msan::UnpoisonMemory(row_in_b + xsize, sizeof(float) * (x1 - xsize));
    if (row_in_a) {
      msan::UnpoisonMemory(row_in_a + xsize, sizeof(float) * (x1 - xsize));
    }

    for (ssize_t x = 0; x < x1; x += Lanes(d)) {
      auto rf = Mul(Clamp(zero, LoadU(d, row_in_r + x), one), mul);
      auto gf = Mul(Clamp(zero, LoadU(d, row_in_g + x), one), mul);
      auto bf = Mul(Clamp(zero, LoadU(d, row_in_b + x), one), mul);
      auto af = row_in_a ? Mul(Clamp(zero, LoadU(d, row_in_a + x), one), mul)
                         : Set(d, 255.0f);
      auto r8 = U8FromU32(BitCast(du, NearestInt(rf)));
      auto g8 = U8FromU32(BitCast(du, NearestInt(gf)));
      auto b8 = U8FromU32(BitCast(du, NearestInt(bf)));
      auto a8 = U8FromU32(BitCast(du, NearestInt(af)));
      size_t n = xsize - x;
      if (JXL_LIKELY(n >= Lanes(d))) {
        StoreRGBA(D::Rebind<uint8_t>(), r8, g8, b8, a8, rgba_, Lanes(d), n,
                  rgb_ + base_ptr + bytes * x);
      } else {
        StoreRGBA(D::Rebind<uint8_t>(), r8, g8, b8, a8, rgba_, n, n,
                  rgb_ + base_ptr + bytes * x);
      }
    }
  }

  RenderPipelineChannelMode GetChannelMode(size_t c) const final {
    return c < 3 || (has_alpha_ && c == alpha_c_)
               ? RenderPipelineChannelMode::kInput
               : RenderPipelineChannelMode::kIgnored;
  }

  const char* GetName() const override { return "WriteToU8"; }

 private:
  uint8_t* rgb_;
  size_t stride_;
  size_t height_;
  bool rgba_;
  bool has_alpha_;
  size_t alpha_c_;
  std::vector<float> opaque_alpha_;
};

std::unique_ptr<RenderPipelineStage> GetWriteToU8Stage(uint8_t* rgb,
                                                       size_t stride,
                                                       size_t height, bool rgba,
                                                       bool has_alpha,
                                                       size_t alpha_c) {
  return jxl::make_unique<WriteToU8Stage>(rgb, stride, height, rgba, has_alpha,
                                          alpha_c);
}

class WriteToPixelCallbackStage : public RenderPipelineStage {
 public:
  WriteToPixelCallbackStage(const PixelCallback& pixel_callback, size_t width,
                            size_t height, size_t num_channels, bool has_alpha,
                            bool unpremul_alpha, size_t alpha_c,
                            bool swap_endianness, Orientation undo_orientation,
                            JxlDataType data_type)
      : RenderPipelineStage(RenderPipelineStage::Settings()),
        pixel_callback_(pixel_callback),
        width_(width),
        height_(height),
        num_channels_(num_channels),
        num_color_(num_channels < 3 ? 1 : 3),
        want_alpha_(num_channels_ == 2 || num_channels_ == 4),
        has_alpha_(has_alpha),
        unpremul_alpha_(unpremul_alpha),
        alpha_c_(alpha_c),
        swap_endianness_(swap_endianness),
        flip_x_(ShouldFlipX(undo_orientation)),
        flip_y_(ShouldFlipY(undo_orientation)),
        transpose_(ShouldTranspose(undo_orientation)),
        data_type_(data_type),
        opaque_alpha_(kMaxPixelsPerCall, 1.0f) {}

  WriteToPixelCallbackStage(const WriteToPixelCallbackStage&) = delete;
  WriteToPixelCallbackStage& operator=(const WriteToPixelCallbackStage&) =
      delete;
  WriteToPixelCallbackStage(WriteToPixelCallbackStage&&) = delete;
  WriteToPixelCallbackStage& operator=(WriteToPixelCallbackStage&&) = delete;

  ~WriteToPixelCallbackStage() override {
    if (run_opaque_) {
      pixel_callback_.destroy(run_opaque_);
    }
  }

  void ProcessRow(const RowInfo& input_rows, const RowInfo& output_rows,
                  size_t xextra, size_t xsize, size_t xpos, size_t ypos,
                  size_t thread_id) const final {
    JXL_DASSERT(run_opaque_);
    if (ypos >= height_) return;
    const float* line_buffers[4];
    for (size_t c = 0; c < num_color_; c++) {
      line_buffers[c] = GetInputRow(input_rows, c, 0) - xextra;
    }
    if (has_alpha_) {
      line_buffers[num_color_] = GetInputRow(input_rows, alpha_c_, 0) - xextra;
    } else {
      // No xextra offset; opaque_alpha_ is a way to set all values to 1.0f.
      line_buffers[num_color_] = opaque_alpha_.data();
    }
    if (flip_y_) {
      ypos = height_ - 1u - ypos;
    }
    // TODO(veluca): SIMD.
    ssize_t limit = std::min(xextra + xsize, width_ - xpos);
    for (ssize_t x0 = -xextra; x0 < limit; x0 += kMaxPixelsPerCall) {
      size_t j = 0;
      size_t ix = 0;
      float* JXL_RESTRICT temp =
          reinterpret_cast<float*>(tempf_[thread_id].get());
      for (; ix < kMaxPixelsPerCall && ssize_t(ix) + x0 < limit; ix++) {
        for (size_t c = 0; c < num_channels_; ++c) {
          temp[j++] = line_buffers[c][ix];
        }
      }
      size_t xstart = xpos + x0;
      size_t xlen = ix;
      size_t len = xlen * num_channels_;
      if (has_alpha_ && want_alpha_ && unpremul_alpha_) {
        // TODO(szabadka) SIMDify (possibly in a separate pipeline stage).
        UnpremultiplyAlpha(temp, num_color_, xlen);
      }
      if (flip_x_) {
        size_t last = (xlen - 1u) * num_channels_;
        size_t num = (xlen / 2) * num_channels_;
        for (size_t i = 0; i < num; i += num_channels_) {
          for (size_t c = 0; c < num_channels_; ++c) {
            std::swap(temp[i + c], temp[last - i + c]);
          }
        }
        xstart = width_ - xstart - xlen;
      }

      const HWY_FULL(float) d;
      auto zero = Zero(d);
      auto one = Set(d, 1.0f);

      const size_t lenup = RoundUpTo(len, Lanes(d));
      msan::UnpoisonMemory(temp + len, sizeof(temp[0]) * (lenup - len));

      if (data_type_ == JXL_TYPE_FLOAT) {
        if (swap_endianness_) {
          for (size_t j = 0; j < len; ++j) {
            temp[j] = BSwapFloat(temp[j]);
          }
        }
        WriteToCallback(thread_id, ypos, xstart, xlen, temp);
      } else if (data_type_ == JXL_TYPE_UINT16 ||
                 data_type_ == JXL_TYPE_FLOAT16) {
        uint16_t* JXL_RESTRICT tempu =
            reinterpret_cast<uint16_t*>(tempu_[thread_id].get());
        const Rebind<uint16_t, decltype(d)> du16;
        if (data_type_ == JXL_TYPE_UINT16) {
          auto mul = Set(d, 65535.0f);
          for (size_t j = 0; j < len; j += Lanes(d)) {
            auto v = Mul(Clamp(zero, LoadU(d, temp + j), one), mul);
            StoreU(DemoteTo(du16, NearestInt(v)), du16, tempu + j);
          }
        } else {
          const Rebind<hwy::float16_t, decltype(d)> df16;
          for (size_t j = 0; j < len; j += Lanes(d)) {
            auto v = LoadU(d, temp + j);
            StoreU(BitCast(du16, DemoteTo(df16, v)), du16, tempu + j);
          }
        }
        if (swap_endianness_) {
          for (size_t j = 0; j < len; ++j) {
            tempu[j] = JXL_BSWAP16(tempu[j]);
          }
        }
        WriteToCallback(thread_id, ypos, xstart, xlen, tempu);
      } else if (data_type_ == JXL_TYPE_UINT8) {
        uint8_t* JXL_RESTRICT tempu =
            reinterpret_cast<uint8_t*>(tempu_[thread_id].get());
        auto mul = Set(d, 255.0f);
        const Rebind<uint8_t, decltype(d)> du8;
        for (size_t j = 0; j < len; j += Lanes(d)) {
          auto v = Mul(Clamp(zero, LoadU(d, temp + j), one), mul);
          StoreU(DemoteTo(du8, NearestInt(v)), du8, tempu + j);
        }
        WriteToCallback(thread_id, ypos, xstart, xlen, tempu);
      }
      for (size_t c = 0; c < num_color_; c++) {
        line_buffers[c] += kMaxPixelsPerCall;
      }
      if (has_alpha_) {
        line_buffers[num_color_] += kMaxPixelsPerCall;
      }
    }
  }

  RenderPipelineChannelMode GetChannelMode(size_t c) const final {
    return c < num_color_ || (has_alpha_ && c == alpha_c_)
               ? RenderPipelineChannelMode::kInput
               : RenderPipelineChannelMode::kIgnored;
  }

  const char* GetName() const override { return "WritePixelCB"; }

 private:
  Status PrepareForThreads(size_t num_threads) override {
    run_opaque_ =
        pixel_callback_.Init(num_threads, /*num_pixels=*/kMaxPixelsPerCall);
    JXL_RETURN_IF_ERROR(run_opaque_ != nullptr);
    tempf_.resize(num_threads);
    for (CacheAlignedUniquePtr& temp : tempf_) {
      temp = AllocateArray(sizeof(float) * kMaxPixelsPerCall * num_channels_);
    }
    if (data_type_ != JXL_TYPE_FLOAT) {
      tempu_.resize(num_threads);
      for (CacheAlignedUniquePtr& temp : tempu_) {
        temp =
            AllocateArray(sizeof(uint16_t) * kMaxPixelsPerCall * num_channels_);
      }
    }
    return true;
  }
  static bool ShouldFlipX(Orientation undo_orientation) {
    return (undo_orientation == Orientation::kFlipHorizontal ||
            undo_orientation == Orientation::kRotate180 ||
            undo_orientation == Orientation::kRotate270 ||
            undo_orientation == Orientation::kAntiTranspose);
  }
  static bool ShouldFlipY(Orientation undo_orientation) {
    return (undo_orientation == Orientation::kFlipVertical ||
            undo_orientation == Orientation::kRotate180 ||
            undo_orientation == Orientation::kRotate90 ||
            undo_orientation == Orientation::kAntiTranspose);
  }
  static bool ShouldTranspose(Orientation undo_orientation) {
    return (undo_orientation == Orientation::kTranspose ||
            undo_orientation == Orientation::kRotate90 ||
            undo_orientation == Orientation::kRotate270 ||
            undo_orientation == Orientation::kAntiTranspose);
  }
  template <typename T>
  void WriteToCallback(size_t thread_id, size_t ypos, size_t xstart,
                       size_t xlen, T* output) const {
    if (transpose_) {
      // TODO(szabadka) Buffer 8x8 chunks and transpose with SIMD.
      for (size_t i = 0, j = 0; i < xlen; ++i, j += num_channels_) {
        pixel_callback_.run(run_opaque_, thread_id, ypos, xstart + i, 1,
                            output + j);
      }
    } else {
      pixel_callback_.run(run_opaque_, thread_id, xstart, ypos, xlen, output);
    }
  }

  static constexpr size_t kMaxPixelsPerCall = 1024;
  PixelCallback pixel_callback_;
  void* run_opaque_ = nullptr;
  size_t width_;
  size_t height_;
  size_t num_channels_;
  size_t num_color_;
  bool want_alpha_;
  bool has_alpha_;
  bool unpremul_alpha_;
  size_t alpha_c_;
  bool swap_endianness_;
  bool flip_x_;
  bool flip_y_;
  bool transpose_;
  JxlDataType data_type_;
  std::vector<float> opaque_alpha_;
  std::vector<CacheAlignedUniquePtr> tempf_;
  std::vector<CacheAlignedUniquePtr> tempu_;
};

std::unique_ptr<RenderPipelineStage> GetWriteToPixelCallbackStage(
    const PixelCallback& pixel_callback, size_t width, size_t height,
    size_t num_channels, bool has_alpha, bool unpremul_alpha, size_t alpha_c,
    bool swap_endianness, Orientation undo_orientation, JxlDataType data_type) {
  return jxl::make_unique<WriteToPixelCallbackStage>(
      pixel_callback, width, height, num_channels, has_alpha, unpremul_alpha,
      alpha_c, swap_endianness, undo_orientation, data_type);
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace jxl
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace jxl {

HWY_EXPORT(GetWriteToU8Stage);
HWY_EXPORT(GetWriteToPixelCallbackStage);

namespace {
class WriteToImageBundleStage : public RenderPipelineStage {
 public:
  explicit WriteToImageBundleStage(ImageBundle* image_bundle,
                                   ColorEncoding color_encoding)
      : RenderPipelineStage(RenderPipelineStage::Settings()),
        image_bundle_(image_bundle),
        color_encoding_(std::move(color_encoding)) {}

  void SetInputSizes(
      const std::vector<std::pair<size_t, size_t>>& input_sizes) override {
#if JXL_ENABLE_ASSERT
    JXL_ASSERT(input_sizes.size() >= 3);
    for (size_t c = 1; c < input_sizes.size(); c++) {
      JXL_ASSERT(input_sizes[c].first == input_sizes[0].first);
      JXL_ASSERT(input_sizes[c].second == input_sizes[0].second);
    }
#endif
    // TODO(eustas): what should we do in the case of "want only ECs"?
    image_bundle_->SetFromImage(
        Image3F(input_sizes[0].first, input_sizes[0].second), color_encoding_);
    // TODO(veluca): consider not reallocating ECs if not needed.
    image_bundle_->extra_channels().clear();
    for (size_t c = 3; c < input_sizes.size(); c++) {
      image_bundle_->extra_channels().emplace_back(input_sizes[c].first,
                                                   input_sizes[c].second);
    }
  }

  void ProcessRow(const RowInfo& input_rows, const RowInfo& output_rows,
                  size_t xextra, size_t xsize, size_t xpos, size_t ypos,
                  size_t thread_id) const final {
    for (size_t c = 0; c < 3; c++) {
      memcpy(image_bundle_->color()->PlaneRow(c, ypos) + xpos - xextra,
             GetInputRow(input_rows, c, 0) - xextra,
             sizeof(float) * (xsize + 2 * xextra));
    }
    for (size_t ec = 0; ec < image_bundle_->extra_channels().size(); ec++) {
      JXL_ASSERT(image_bundle_->extra_channels()[ec].xsize() >=
                 xpos + xsize + xextra);
      memcpy(image_bundle_->extra_channels()[ec].Row(ypos) + xpos - xextra,
             GetInputRow(input_rows, 3 + ec, 0) - xextra,
             sizeof(float) * (xsize + 2 * xextra));
    }
  }

  RenderPipelineChannelMode GetChannelMode(size_t c) const final {
    return RenderPipelineChannelMode::kInput;
  }

  const char* GetName() const override { return "WriteIB"; }

 private:
  ImageBundle* image_bundle_;
  ColorEncoding color_encoding_;
};

class WriteToImage3FStage : public RenderPipelineStage {
 public:
  explicit WriteToImage3FStage(Image3F* image)
      : RenderPipelineStage(RenderPipelineStage::Settings()), image_(image) {}

  void SetInputSizes(
      const std::vector<std::pair<size_t, size_t>>& input_sizes) override {
#if JXL_ENABLE_ASSERT
    JXL_ASSERT(input_sizes.size() >= 3);
    for (size_t c = 1; c < 3; ++c) {
      JXL_ASSERT(input_sizes[c].first == input_sizes[0].first);
      JXL_ASSERT(input_sizes[c].second == input_sizes[0].second);
    }
#endif
    *image_ = Image3F(input_sizes[0].first, input_sizes[0].second);
  }

  void ProcessRow(const RowInfo& input_rows, const RowInfo& output_rows,
                  size_t xextra, size_t xsize, size_t xpos, size_t ypos,
                  size_t thread_id) const final {
    for (size_t c = 0; c < 3; c++) {
      memcpy(image_->PlaneRow(c, ypos) + xpos - xextra,
             GetInputRow(input_rows, c, 0) - xextra,
             sizeof(float) * (xsize + 2 * xextra));
    }
  }

  RenderPipelineChannelMode GetChannelMode(size_t c) const final {
    return c < 3 ? RenderPipelineChannelMode::kInput
                 : RenderPipelineChannelMode::kIgnored;
  }

  const char* GetName() const override { return "WriteI3F"; }

 private:
  Image3F* image_;
};

}  // namespace

std::unique_ptr<RenderPipelineStage> GetWriteToImageBundleStage(
    ImageBundle* image_bundle, ColorEncoding color_encoding) {
  return jxl::make_unique<WriteToImageBundleStage>(image_bundle,
                                                   std::move(color_encoding));
}

std::unique_ptr<RenderPipelineStage> GetWriteToImage3FStage(Image3F* image) {
  return jxl::make_unique<WriteToImage3FStage>(image);
}

std::unique_ptr<RenderPipelineStage> GetWriteToU8Stage(uint8_t* rgb,
                                                       size_t stride,
                                                       size_t height, bool rgba,
                                                       bool has_alpha,
                                                       size_t alpha_c) {
  return HWY_DYNAMIC_DISPATCH(GetWriteToU8Stage)(rgb, stride, height, rgba,
                                                 has_alpha, alpha_c);
}

std::unique_ptr<RenderPipelineStage> GetWriteToPixelCallbackStage(
    const PixelCallback& pixel_callback, size_t width, size_t height,
    size_t num_channels, bool has_alpha, bool unpremul_alpha, size_t alpha_c,
    bool swap_endianness, Orientation undo_orientation, JxlDataType data_type) {
  return HWY_DYNAMIC_DISPATCH(GetWriteToPixelCallbackStage)(
      pixel_callback, width, height, num_channels, has_alpha, unpremul_alpha,
      alpha_c, swap_endianness, undo_orientation, data_type);
}

}  // namespace jxl

#endif
