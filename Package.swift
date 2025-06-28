// swift-tools-version: 6.1

/*===----------------------------------------------------------------------===*/
/*                                                        ___   ___           */
/* Package.swift                                        /'___\ /\_ \          */
/*                                                     /\ \__/ \//\ \         */
/* Author: Fang Ling (fangling@fangl.ing)              \ \ ,__\  \ \ \        */
/* Date: June 28, 2025                                  \ \ \_/__ \_\ \_  __  */
/*                                                       \ \_\/\_\/\____\/\_\ */
/* Copyright (c) 2025-2025 Fang Ling All Rights Reserved. \/_/\/_/\/____/\/_/ */
/*===----------------------------------------------------------------------===*/

import PackageDescription

let package = Package(
  name: "swift-jpeg-xl",
  products: [
    .library(name: "CJPEGXL", targets: ["CJPEGXL"])
  ],
  targets: [
    .target(
      name: "CJPEGXL",
      dependencies: ["hwy", "brotli", "lcms"],
      path: ".",
      exclude: [
        "lib/jxl/test_memory_manager.cc",
        "lib/jxl/opsin_image_test.cc",
        "lib/jxl/bits_test.cc",
        "lib/jxl/padded_bytes_test.cc",
        "lib/jxl/encode_test.cc",
        "lib/jxl/render_pipeline/render_pipeline_test.cc",
        "lib/jxl/coeff_order_test.cc",
        "lib/jxl/rational_polynomial_test.cc",
        "lib/jxl/iaca_test.cc",
        "lib/jxl/passes_test.cc",
        "lib/jxl/blending_test.cc",
        "lib/jxl/image_ops_test.cc",
        "lib/jxl/butteraugli/butteraugli_test.cc",
        "lib/jxl/preview_test.cc",
        "lib/jxl/ac_strategy_test.cc",
        "lib/jxl/data_parallel_test.cc",
        "lib/jxl/fields_test.cc",
        "lib/jxl/gamma_correct_test.cc",
        "lib/jxl/jxl_test.cc",
        "lib/jxl/speed_tier_test.cc",
        "lib/jxl/simd_util_test.cc",
        "lib/jxl/lehmer_code_test.cc",
        "lib/jxl/quantizer_test.cc",
        "lib/jxl/toc_test.cc",
        "lib/jxl/icc_codec_test.cc",
        "lib/jxl/enc_photon_noise_test.cc",
        "lib/jxl/convolve_test.cc",
        "lib/jxl/dct_test.cc",
        "lib/jxl/patch_dictionary_test.cc",
        "lib/jxl/splines_test.cc",
        "lib/jxl/ans_common_test.cc",
        "lib/jxl/quant_weights_test.cc",
        "lib/jxl/opsin_inverse_test.cc",
        "lib/jxl/ans_test.cc",
        "lib/jxl/gradient_test.cc",
        "lib/jxl/modular_test.cc",
        "lib/jxl/byte_order_test.cc",
        "lib/jxl/cms/transfer_functions_test.cc",
        "lib/jxl/cms/tone_mapping_test.cc",
        "lib/jxl/roundtrip_test.cc",
        "lib/jxl/xorshift128plus_test.cc",
        "lib/jxl/color_encoding_internal_test.cc",
        "lib/jxl/bit_reader_test.cc",
        "lib/jxl/enc_external_image_test.cc",
        "lib/jxl/enc_optimize_test.cc",
        "lib/jxl/enc_linalg_test.cc",
        "lib/jxl/color_management_test.cc",
        "lib/jxl/alpha_test.cc",
        "lib/jxl/decode_test.cc",
        "lib/jxl/image_bundle_test.cc",
        "lib/jxl/fast_math_test.cc",
        "lib/jxl/enc_gaborish_test.cc",
        "lib/jxl/entropy_coder_test.cc",
        "lib/jxl/enc_bit_writer_test.cc",
        "lib/jxl/dec_transforms_testonly.h",
        "lib/jxl/dec_transforms_testonly.cc",
        "lib/jxl/test_image.h",
        "lib/jxl/test_image.cc",
        "lib/jxl/test_utils.h",
        "lib/jxl/test_utils.cc",
        "lib/jxl/enc_external_image_gbench.cc",
        "lib/jxl/splines_gbench.cc",
        "lib/jxl/dec_external_image_gbench.cc",
        "lib/jxl/tf_gbench.cc",
        "lib/jxl/dct_gbench.cc",
        "lib/threads/thread_parallel_runner_test.cc"
      ],
      sources: ["lib/jxl", "lib/threads"],
      publicHeadersPath: "lib/include",
      cSettings: [
        .headerSearchPath("."),
        .define("CMS_NO_REGISTER_KEYWORD")
      ]
    ),
    .target(
      name: "hwy",
      path: "third_party/highway",
      exclude: [
        "hwy/tests",
        "hwy/examples",
        "hwy/contrib",
        "hwy/base_test.cc",
        "hwy/nanobenchmark_test.cc",
        "hwy/targets_test.cc",
        "hwy/bit_set_test.cc",
        "hwy/abort_test.cc",
        "hwy/highway_test.cc",
        "hwy/aligned_allocator_test.cc"
      ],
      publicHeadersPath: "."
    ),
    .target(
      name: "brotli",
      path: "third_party/brotli/c",
      exclude: ["fuzz", "tools"],
      publicHeadersPath: "include"
    ),
    .target(
      name: "lcms",
      path: "third_party/lcms",
      exclude: [
        "src/Makefile.am",
        "src/Makefile.in"
      ],
      sources: ["src"],
      publicHeadersPath: "include"
    )
  ],
  cLanguageStandard: .gnu11,
  cxxLanguageStandard: .gnucxx11
)

/*===----------------------------------------------------------------------===*/
/*         ____     _____  ____    ____    ____    __   __   __               */
/*        /\  _`\  /\___ \/\  _`\ /\  _`\ /\  _`\ /\ \ /\ \ /\ \              */
/*        \ \ \/\_\\/__/\ \ \ \L\ \ \ \L\_\ \ \L\_\ `\`\/'/'\ \ \             */
/*         \ \ \/_/_  _\ \ \ \ ,__/\ \  _\L\ \ \L_L`\/ > <   \ \ \  __        */
/*          \ \ \L\ \/\ \_\ \ \ \/  \ \ \L\ \ \ \/, \ \/'/\`\ \ \ \L\ \       */
/*           \ \____/\ \____/\ \_\   \ \____/\ \____/ /\_\\ \_\\ \____/       */
/*            \/___/  \/___/  \/_/    \/___/  \/___/  \/_/ \/_/ \/___/        */
/*===----------------------------------------------------------------------===*/
