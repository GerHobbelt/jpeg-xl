// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "benchmark/benchmark.h"

#ifndef BUILD_MONOLITHIC

BENCHMARK_MAIN();

#else

extern "C" int gbench_benchmark_main(int argc, const char **argv)
{
	// a main routine that runs the benchmarks
	// Note the workaround for Hexagon simulator passing argc != 0, argv = NULL.
    const char arg0_default[] = "benchmark";                                  
    const char* args_default = arg0_default;                                  
    if (!argv) {                                                        
      argc = 1;                                                         
      argv = &args_default;                                             
    }                                                                   
    ::benchmark::Initialize(&argc, argv);                               
    if (::benchmark::ReportUnrecognizedArguments(argc, argv))
		return 1; 
    ::benchmark::RunSpecifiedBenchmarks();                              
    ::benchmark::Shutdown();                                            
    return 0;                                                           
}

#endif
