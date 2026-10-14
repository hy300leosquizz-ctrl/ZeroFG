// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit's stand-in for Xenia's platform switches. The
// presenter is Android only: it moves frames as AHardwareBuffers with sync
// files and presents on an ANativeWindow.
#ifndef ZEROFG_PORTABLE_XENIA_BASE_PLATFORM_H_
#define ZEROFG_PORTABLE_XENIA_BASE_PLATFORM_H_

#if defined(__ANDROID__)
#define XE_PLATFORM_ANDROID 1
#else
#define XE_PLATFORM_ANDROID 0
#endif

#endif  // ZEROFG_PORTABLE_XENIA_BASE_PLATFORM_H_
