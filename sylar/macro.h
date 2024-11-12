/**
 * @file macro.h
 * @brief 常用宏的封装
 * @author sylar.yin
 * @email 564628276@qq.com
 * @date 2019-06-01
 * @copyright Copyright (c) 2019年 sylar.yin All rights reserved (www.sylar.top)
 */
#ifndef __SYLAR_MACRO_H__
#define __SYLAR_MACRO_H__

#include <string.h>
#include <assert.h>
#include "log.h"
#include "util.h"

#if defined __GNUC__ || defined __llvm__
/// LIKCLY 宏的封装, 告诉编译器优化,条件大概率成立 即SYLAR_LIKELY(x)大概率为真
#define SYLAR_LIKELY(x) __builtin_expect(!!(x), 1)
/// LIKCLY 宏的封装, 告诉编译器优化,条件大概率不成立 即SYLAR_UNLIKELY(x)大概率为假
#define SYLAR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define SYLAR_LIKELY(x) (x)
#define SYLAR_UNLIKELY(x) (x)
#endif

/*
    if(likely(value))  // 等价于 if(value) 只不过value可能为真的可能性更大。
    if(unlikely(value))  // 也等价于 if(value) 只不过value可能为假的可能性更大
*/

/// 断言宏封装 SYLAR_ASSERT(x) 当x为真时不会断言（中断）
#define SYLAR_ASSERT(x)                                                                \
    if (SYLAR_UNLIKELY(!(x))) {                                                        \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION: " #x                          \
                                          << "\nbacktrace:\n"                          \
                                          << sylar::BacktraceToString(100, 2, "    "); \
        assert(x);                                                                     \
    }

/// 断言宏封装
#define SYLAR_ASSERT2(x, w)                                                            \
    if (SYLAR_UNLIKELY(!(x))) {                                                        \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION: " #x                          \
                                          << "\n"                                      \
                                          << w                                         \
                                          << "\nbacktrace:\n"                          \
                                          << sylar::BacktraceToString(100, 2, "    "); \
        assert(x);                                                                     \
    }

#endif
