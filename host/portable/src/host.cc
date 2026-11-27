// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later

#include "zerofg_host/host.h"

#include <atomic>

#include "xenia/base/cvar.h"

// The settings the presenter declares (zerofg_config.h). The host sets them
// before it creates the presenter.
DEFINE_bool(zerofg_frame_generation, false, "", "ZeroFG");
DEFINE_string(zerofg_mode, "off", "", "ZeroFG");
DEFINE_uint32(zerofg_fps_limit, 0, "", "ZeroFG");
DEFINE_bool(zerofg_guest_vblank_pull, true, "", "ZeroFG");
// MFG: the most outputs per pair the user allows (2, 3 or 4); the presenter
// caps it by what the display refresh holds.
DEFINE_uint32(zerofg_multiplier, 2, "", "ZeroFG");
// Pacing tools (host/presenter/README.md, "Tuning a host's pacing"). The
// defaults are the ones measured in play on three hosts; a host changes one
// only after measuring its own output.
DEFINE_bool(zerofg_phase_recovery, true, "", "ZeroFG");
DEFINE_bool(zerofg_free_output, true, "", "ZeroFG");
DEFINE_bool(zerofg_vsync_quantizer, true, "", "ZeroFG");
// On in the pacing-shadow test builds (2026-11-27): ARMSX2 at a 30 lock on 60 Hz handed each output to the egress
// at its target (present call vs desired p50 -0.06 ms) and the sum reach (copy + a refresh + margin) put it two
// vsyncs out, ready 27 ms before its vsync. The tight reach keeps its own session fallback (5 % late).
DEFINE_bool(zerofg_vsync_tight_reach, true, "", "ZeroFG");
// On in the pacing-shadow test builds (2026-11-27): JoJo at a 30 lock on 60 Hz showed the quantizer's phase
// pushed one vsync for long stretches (Real age +13 ms), with no free vsync at one output per refresh to shed into.
DEFINE_bool(zerofg_vsync_retire, true, "", "ZeroFG");
// No latency wells at one output per refresh (N x the game's rate = the
// refresh): recoveries anchored at the oldest waiting Real, short pairs.
DEFINE_bool(zerofg_mfg_tick_retire, true, "", "ZeroFG");

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
