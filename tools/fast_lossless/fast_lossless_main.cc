// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifdef HAVE_LODEPNG

#include "lib/jxl/enc_fast_lossless.h"
#include "pam-input.h"

#include "monolithic_examples.h"



#if defined(BUILD_MONOLITHIC)
#define main(cnt, arr) jpegXL_fast_lossless_main(cnt, arr)
#endif

int main(int argc, const char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage: %s in.png out.jxl [effort] [num_reps] [num_threads]\n",
            argv[0]);
    return 1;
  }

  const char* in = argv[1];
  const char* out = argv[2];
  int effort = argc >= 4 ? atoi(argv[3]) : 2;
  size_t num_reps = argc >= 5 ? atoi(argv[4]) : 1;
  size_t num_threads = argc >= 6 ? atoi(argv[5]) : 0;

  if (effort < 0 || effort > 127) {
    fprintf(
        stderr,
        "Effort should be between 0 and 127 (default is 2, more is slower)\n");
    return 1;
  }

  unsigned char* png;
  unsigned w;
  unsigned h;
  unsigned error = lodepng_decode32_file(&png, &w, &h, in);

  size_t nb_chans = 4;
  size_t bitdepth = 8;
  size_t width = w;
  size_t height = h;
  if (error && !DecodePAM(in, &png, &width, &height, &nb_chans, &bitdepth)) {
    fprintf(stderr, "lodepng error %u: %s\n", error, lodepng_error_text(error));
    return 1;
  }

  auto parallel_runner = [](void* num_threads_ptr, void* opaque,
                            void fun(void*, size_t), size_t count) {
    size_t num_threads = *static_cast<size_t*>(num_threads_ptr);
    if (num_threads == 0) {
      num_threads = std::thread::hardware_concurrency();
    }
    if (num_threads > count) {
      num_threads = count;
    }
    if (num_threads == 1) {
      for (size_t i = 0; i < count; i++) {
        fun(opaque, i);
      }
    } else {
      std::atomic<uint32_t> task{0};
      std::vector<std::thread> threads;
      for (size_t i = 0; i < num_threads; i++) {
        threads.push_back(std::thread([count, opaque, fun, &task]() {
          while (true) {
            uint32_t t = task++;
            if (t >= count) break;
            fun(opaque, t);
          }
        }));
      }
      for (auto& t : threads) t.join();
    }
  };

  size_t encoded_size = 0;
  unsigned char* encoded = nullptr;
  size_t stride = width * nb_chans * (bitdepth > 8 ? 2 : 1);

  auto start = std::chrono::high_resolution_clock::now();
  for (size_t _ = 0; _ < num_reps; _++) {
    free(encoded);
    encoded = nullptr;
    encoded_size = JxlFastLosslessEncode(
        png, width, stride, height, nb_chans, bitdepth,
        /*big_endian=*/true, effort, &encoded, &num_threads, +parallel_runner);
    if (encoded_size == 0) return EXIT_FAILURE;
  }
  auto stop = std::chrono::high_resolution_clock::now();
  if (num_reps > 1) {
    float us =
        std::chrono::duration_cast<std::chrono::microseconds>(stop - start)
            .count();
    size_t pixels = size_t{width} * size_t{height} * num_reps;
    float mps = pixels / us;
    fprintf(stderr, "%10.3f MP/s\n", mps);
    fprintf(stderr, "%10.3f bits/pixel\n",
            encoded_size * 8.0 / static_cast<float>(width) /
                static_cast<float>(height));
  }

  FILE* o = fopen(out, "wb");
  if (!o) {
    fprintf(stderr, "error opening %s: %s\n", out, strerror(errno));
    return 1;
  }
  if (fwrite(encoded, 1, encoded_size, o) != encoded_size) {
    fprintf(stderr, "error writing to %s: %s\n", out, strerror(errno));
  }
  fclose(o);
}

#endif
