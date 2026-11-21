#ifndef XENIA_UI_VULKAN_ZEROFG_CONFIG_H_
#define XENIA_UI_VULKAN_ZEROFG_CONFIG_H_

#include <atomic>
#include <cstdint>

#include "xenia/base/cvar.h"

DECLARE_bool(zerofg_frame_generation);
DECLARE_string(zerofg_mode);
DECLARE_uint32(zerofg_fps_limit);
DECLARE_bool(zerofg_guest_vblank_pull);
// Pacing tools (README.md, "Tuning a host's pacing"): phase recovery, free
// output and the vsync quantizer default on, the tight reach off.
DECLARE_bool(zerofg_phase_recovery);
DECLARE_bool(zerofg_free_output);
DECLARE_bool(zerofg_vsync_quantizer);
DECLARE_bool(zerofg_vsync_tight_reach);
DECLARE_bool(zerofg_vsync_retire);
DECLARE_bool(zerofg_mfg_tick_retire);
// MFG: the most outputs per pair the user allows (2, 3 or 4; an internal build with kZeroFGMfgMaxN 4 reads it).
DECLARE_uint32(zerofg_multiplier);
namespace xe::ui::vulkan {

enum class ZeroFGSelection { kOff, kZero, kReallyZero };

// zerofg::CreateInfo::zero_options: none (Zero light and the x4 quarters were retired with the old Zero, which no mode selects now).
inline uint32_t ZeroFGZeroOptions() { return 0u; }

inline ZeroFGSelection StartupZeroFGSelection() {
  const std::string& mode = cvars::zerofg_mode;
  if (mode == "off") {
    return ZeroFGSelection::kOff;
  }
  // The owner, 2026-11-21: ZeroFG is On or Off, and On is RZ+ (the core's ReallyZero). Every earlier name that meant on, the old Zero
  // ("zero", Heavy Zero in the test builds) included, keeps frame generation on with RZ+.
  if (mode == "reallyzero" || mode == "rc3_lite" || mode == "zero" || mode == "rc3" || mode == "subzero" || mode == "rc1" ||
      mode == "rc1_golden" || mode == "rc1_test" || mode == "rc2" || (mode.empty() && cvars::zerofg_frame_generation)) {
    return ZeroFGSelection::kReallyZero;
  }
  return ZeroFGSelection::kOff;
}

// Runtime requests are consumed by the presenter owner thread at reconnect.
inline std::atomic<int>& ZeroFGRuntimeModeOverride() {
  static std::atomic<int> mode{-1};
  return mode;
}
inline void RequestZeroFGMode(uint32_t mode) {
  ZeroFGRuntimeModeOverride().store(mode <= 2 ? int(mode) : 0,
                                  std::memory_order_release);
}
inline ZeroFGSelection GetZeroFGSelection() {
  switch (ZeroFGRuntimeModeOverride().load(std::memory_order_acquire)) {
    case 0: return ZeroFGSelection::kOff;
    case 1:
    case 2: return ZeroFGSelection::kReallyZero;  // On is RZ+ (see StartupZeroFGSelection)
    default: return StartupZeroFGSelection();
  }
}
inline uint32_t RequestedZeroFGMode() {
  switch (GetZeroFGSelection()) {
    case ZeroFGSelection::kReallyZero: return 1;
    case ZeroFGSelection::kZero: return 2;
    default: return 0;
  }
}

inline bool IsZeroFGRequested() {
  return GetZeroFGSelection() != ZeroFGSelection::kOff;
}

// True while a ZeroFG presenter is connected and has not failed open. The
// guest-side pacing (the vblank pull and the game frame rate cap) acts only
// then: a session that fell back to the native path, or whose device B never
// qualified, runs the guest natively.
inline std::atomic<bool>& ZeroFGGuestPacingLive() {
  static std::atomic<bool> live{false};
  return live;
}

inline bool IsReallyZeroRequested() {
  return GetZeroFGSelection() == ZeroFGSelection::kReallyZero;
}

inline const char* ZeroFGSelectionName() {
  switch (GetZeroFGSelection()) {
    case ZeroFGSelection::kOff:
      return "OFF";
    case ZeroFGSelection::kZero:
      return "ZERO";
    case ZeroFGSelection::kReallyZero:
      return "REALLYZERO";
  }
  return "UNSUPPORTED";
}

}  // namespace xe::ui::vulkan

#endif  // XENIA_UI_VULKAN_ZEROFG_CONFIG_H_
