// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_PRINTF_MACROS_H_
#define LIB_JXL_BASE_PRINTF_MACROS_H_

// Format string macros. These should be included after any other system
// library since those may unconditionally define these, depending on the
// platform.

// PRIuS and PRIdS macros to print size_t and ssize_t respectively.
#if !defined(PRIdS)
#define PRIdS "zd"
#endif  // PRIdS

#if !defined(PRIuS)
#define PRIuS "zu"
#endif  // PRIuS

#endif  // LIB_JXL_BASE_PRINTF_MACROS_H_
