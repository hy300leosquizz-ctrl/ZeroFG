// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit's stand-in for Xenia's logging: the XELOG macros
// format with {fmt} and hand the line to the host's sink (zerofg_host/host.h).
#ifndef ZEROFG_PORTABLE_XENIA_BASE_LOGGING_H_
#define ZEROFG_PORTABLE_XENIA_BASE_LOGGING_H_

#include "fmt/format.h"
#include "zerofg_host/host.h"

#define XELOGE(...) \
  ::zerofg_host::Log(::zerofg_host::LogLevel::kError, fmt::format(__VA_ARGS__))
#define XELOGW(...)                                     \
  ::zerofg_host::Log(::zerofg_host::LogLevel::kWarning, \
                     fmt::format(__VA_ARGS__))
#define XELOGI(...) \
  ::zerofg_host::Log(::zerofg_host::LogLevel::kInfo, fmt::format(__VA_ARGS__))
#define XELOGD(...) \
  ::zerofg_host::Log(::zerofg_host::LogLevel::kDebug, fmt::format(__VA_ARGS__))

#endif  // ZEROFG_PORTABLE_XENIA_BASE_LOGGING_H_
