#pragma once

#include <memory>

#include "zerofg/zerofg.h"

namespace zerofg {

class AlgorithmContext {
 public:
  virtual ~AlgorithmContext() = default;

  virtual Status Resize(uint32_t width, uint32_t height,
                        VkFormat input_format,
                        VkFormat output_format) = 0;

  virtual Status Interpolate(VkCommandBuffer command_buffer,
                             const Image& previous,
                             const Image& current, float phase,
                             const Image& output) = 0;

  // The screen as the host knows it (the HUD planes: the HUD layer's code image, src/hud/hud_layer.h), for the records that follow until
  // it is cleared (nullptr). An engine without a screen of its own ignores it; Zero adds it to its own guess
  // (shaders/rc7/rc7_hudgs.comp).
  virtual void SetScreenTruth(const Image* /*screen*/) {}

  // MFG (x3/x4): the next Interpolate is another phase of the pair this context recorded last (Interpolator::Interpolate decides it from
  // the sequences and the geometry). The engine then records only what depends on the phase: the per-pair work (the flow, Zero's
  // surfaces and layers down to the mattes) is the last record's. Cleared after the call.
  virtual void SetPairRepeat(bool /*repeat*/) {}
};

// ReallyZero, the dense flow (Mode::kReallyZero): one context whatever the frame context index.
std::unique_ptr<AlgorithmContext> CreateDfContext(const CreateInfo& create_info, Status* status);
// Zero, RC7 (Mode::kZero): one context whatever the frame context index.
std::unique_ptr<AlgorithmContext> CreateRc7Context(const CreateInfo& create_info, Status* status);

// The dense flow as a source for Zero: it solves the A -> B flow down to `finest` and, on request, the middle picture Zero builds on.
// The flow is a vec2 per pixel of the finest level, in that level's pixels, at flow_offset() vec2s into flow_buffer(), written by the
// compute stage.
class DenseFlowSource {
 public:
  virtual ~DenseFlowSource() = default;
  virtual Status Resize(uint32_t width, uint32_t height, VkFormat input_format) = 0;
  virtual Status RecordFlow(VkCommandBuffer command_buffer, const Image& previous, const Image& current) = 0;
  // The flow and the dense-flow middle picture.
  virtual Status RecordPicture(VkCommandBuffer command_buffer, const Image& previous, const Image& current, const Image& output) = 0;
  // The flow from `previous` and `current`, the picture's last steps from larger Reals into a larger image: their size, set before Resize.
  virtual void SetPictureSize(uint32_t width, uint32_t height) = 0;
  virtual Status RecordPictureFrom(VkCommandBuffer command_buffer, const Image& previous, const Image& current, const Image& picture_previous, const Image& picture_current, const Image& output) = 0;
  // For the next RecordPicture / RecordPictureFrom only: the picture at `phase` instead of the middle, and with `repeat` the pair of the
  // last record again (its flow is kept; only the picture is recorded). Reset by that call.
  virtual void SetPicturePhase(float phase, bool repeat) = 0;
  // The pair to come does not continue the last one (the caller knows it from its own identities): the flow starts from nothing.
  virtual void ForgetStart() = 0;
  virtual VkBuffer flow_buffer() const = 0;
  virtual uint32_t flow_offset() const = 0;
  virtual uint32_t flow_width() const = 0;
  virtual uint32_t flow_height() const = 0;
};
// Optional exact pyramid retention is separate from the temporal flow starts.
// It preserves the FP32 flow-buffer contract; callers opt in explicitly.
std::unique_ptr<DenseFlowSource> CreateDenseFlowSource(const CreateInfo& create_info, uint32_t finest, Status* status, bool starts = false, bool retain_pyramid = false);

}  // namespace zerofg
