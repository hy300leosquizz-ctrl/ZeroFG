// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0

#include "zerofg_host/host.h"

#include <atomic>

#include "xenia/base/cvar.h"

// The settings the presenter declares (zerofg_config.h). The host sets them
// before it creates the presenter.
DEFINE_bool(zerofg_frame_generation, false, "", "ZeroFG");
DEFINE_string(zerofg_mode, "off", "", "ZeroFG");
DEFINE_uint32(zerofg_fps_limit, 0, "", "ZeroFG");
// Pacing tools (host/presenter/README.md, "Tuning a host's pacing"). The
// defaults are the ones measured in play on three hosts; a host changes one
// only after measuring its own output.
DEFINE_bool(zerofg_phase_recovery, true, "", "ZeroFG");
DEFINE_bool(zerofg_free_output, true, "", "ZeroFG");
DEFINE_bool(zerofg_vsync_quantizer, true, "", "ZeroFG");
DEFINE_bool(zerofg_vsync_tight_reach, false, "", "ZeroFG");

namespace zerofg_host {

namespace {
std::atomic<LogSink> g_log_sink{nullptr};
}  // namespace

void SetLogSink(LogSink sink) {
  g_log_sink.store(sink, std::memory_order_release);
}

void Log(LogLevel level, std::string_view message) {
  if (const LogSink sink = g_log_sink.load(std::memory_order_acquire)) {
    sink(level, message);
  }
}

}  // namespace zerofg_host
