// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit: what a host gives the ZeroFG presenter besides its
// Vulkan handles. The presenter was written inside XenDroid against a few
// Xenia types; the headers under include/xenia/ are small stand-ins for
// exactly those, so the presenter builds unchanged in another host.
#ifndef ZEROFG_PORTABLE_HOST_H_
#define ZEROFG_PORTABLE_HOST_H_

#include <cstddef>
#include <string_view>

namespace zerofg_host {

enum class LogLevel { kError, kWarning, kInfo, kDebug };

// Receives every presenter log line, already formatted, without a trailing
// newline. Called from the presenter's threads; must be thread safe. Unset,
// the lines go nowhere.
using LogSink = void (*)(LogLevel level, std::string_view message);
void SetLogSink(LogSink sink);
void Log(LogLevel level, std::string_view message);

}  // namespace zerofg_host

#endif  // ZEROFG_PORTABLE_HOST_H_
