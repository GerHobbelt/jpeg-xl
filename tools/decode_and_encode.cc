// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdio.h>

#include <cstdint>
#include <string>
#include <vector>

#include "lib/extras/codec.h"
#include "lib/extras/dec/color_hints.h"
#include "lib/extras/dec/decode.h"
#include "lib/extras/packed_image.h"
#include "lib/jxl/base/span.h"
#include "lib/jxl/color_encoding_internal.h"
#include "tools/file_io.h"
#include "tools/thread_pool_internal.h"

#include "monolithic_examples.h"

namespace {

// Reads an input file (typically PNM) with color_space hint and writes to an
// output file (typically PNG) which supports all required metadata.
static int Convert(int argc, const char** argv) {
  if (argc != 4) {
    fprintf(stderr, "Args: in colorspace_description out\n");
    return 1;
  }
  const std::string& pathname_in = argv[1];
  const std::string& desc = argv[2];
  const std::string& pathname_out = argv[3];

  std::vector<uint8_t> encoded_in;
  if (!jpegxl::tools::ReadFile(pathname_in, &encoded_in)) {
    fprintf(stderr, "Failed to read image from %s\n", pathname_in.c_str());
    return 1;
  }
  jxl::extras::PackedPixelFile ppf;
  jxl::extras::ColorHints color_hints;
  jpegxl::tools::ThreadPoolInternal pool(4);
  color_hints.Add("color_space", desc);
  if (!jxl::extras::DecodeBytes(jxl::Bytes(encoded_in), color_hints, &ppf)) {
    fprintf(stderr, "Failed to decode %s\n", pathname_in.c_str());
    return 1;
  }

  jxl::ColorEncoding internal;
  if (!internal.FromExternal(ppf.color_encoding) || internal.ICC().empty()) {
    fprintf(stderr,
            "Failed to generate ICC profile from colorspace description\n");
    return 1;
  }
  // Roundtrip so that the chromaticities are populated even for enum values.
  ppf.color_encoding = internal.ToExternal();
  ppf.icc = internal.ICC();

  std::vector<uint8_t> encoded_out;
  if (!jxl::Encode(ppf, pathname_out, &encoded_out, pool.get())) {
    fprintf(stderr, "Failed to encode %s\n", pathname_out.c_str());
    return 1;
  }
  if (!jpegxl::tools::WriteFile(pathname_out, encoded_out)) {
    fprintf(stderr, "Failed to write %s\n", pathname_out.c_str());
    return 1;
  }

  return 0;
}

}  // namespace




#if defined(BUILD_MONOLITHIC)
#define main(cnt, arr) jpegXL_dec_enc_main(cnt, arr)
#endif

/*
 * The main program.
 */

int main(int argc, const char** argv) {
  return Convert(argc, argv);
}
