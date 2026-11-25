// Copyright (c) 2026 seidnerj
// SPDX-License-Identifier: BSD-3-Clause
// Kernel module descriptor, the equivalent of the _info.c Xcode generates for a kext target.
#include <mach/mach_types.h>

#define xStr(x) #x
#define Str(x) xStr(x)
#define xCat(a, b) a ## b
#define Cat(a, b) xCat(a, b)

extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
extern kern_return_t Cat(PRODUCT_NAME, _kern_start)(kmod_info_t *, void *);
extern kern_return_t Cat(PRODUCT_NAME, _kern_stop)(kmod_info_t *, void *);

// KMOD_EXPLICIT_DECL stringifies its name argument, so expand BUNDLE_ID first.
#define KmodDecl(name, version) KMOD_EXPLICIT_DECL(name, version, _start, _stop)
__attribute__((visibility("default"))) KmodDecl(BUNDLE_ID, Str(MODULE_VERSION))
__private_extern__ kmod_start_func_t *_realmain = Cat(PRODUCT_NAME, _kern_start);
__private_extern__ kmod_stop_func_t *_antimain = Cat(PRODUCT_NAME, _kern_stop);
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
