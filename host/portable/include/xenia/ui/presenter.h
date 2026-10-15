// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit's stand-in for Xenia's Presenter. The ZeroFG presenter
// only carries the output settings from the host's PaintConfigProvider to the
// host's PostProcessCallback, unread, so their shape is the host's: the fields
// below are what a plain scaled present needs, and `host` carries anything else.
#ifndef ZEROFG_PORTABLE_XENIA_UI_PRESENTER_H_
#define ZEROFG_PORTABLE_XENIA_UI_PRESENTER_H_

#include <array>
#include <memory>

// The ZeroFG presenter's header reaches the Source timing types through this
// one, as it does in a Xenia-based host.
#include "xenia/base/frame_stats.h"

namespace xe {
namespace ui {

class Presenter {
 public:
  struct GuestOutputPaintConfig {
    // Where the frame goes in the final output, in its pixels: left, top,
    // right, bottom. All zero: the whole output.
    std::array<float, 4> draw_rect = {};
    // Bilinear scaling, else nearest.
    bool linear_filter = true;
    // Whatever else the host's post needs.
    std::shared_ptr<const void> host;
  };
};

}  // namespace ui
}  // namespace xe

#endif  // ZEROFG_PORTABLE_XENIA_UI_PRESENTER_H_
