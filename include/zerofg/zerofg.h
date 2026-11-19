#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>
#include "zerofg/backend_capabilities.h"

namespace zerofg {

// ZeroFG deliberately does not own a Vulkan queue, swapchain, presentation,
// frame pacing, or synchronization with the host renderer.
//
// The host is responsible for:
//   - beginning/ending and submitting the command buffer;
//   - synchronization and image barriers;
//   - keeping input/output images alive;
//   - presenting the generated frame.
//
// ZeroFG only records temporal interpolation work into the supplied
// VkCommandBuffer.

enum class Status {
  kSuccess = 0,
  kInvalidArgument,
  kUnsupported,
  kOutOfMemory,
  kVulkanError,
};

// The two engines (ZeroFG 1.1; in 1.0 both names meant the RC3 engine, which
// is gone).
enum class Mode : uint8_t {
  // Zero, the quality engine (RC7): the pair explained as layers (surfaces
  // that move as a whole, the nearest moving object with its own matte, a
  // screen layer for what holds still on the screen) over the dense-flow
  // picture, which a judge takes back where the layers contradict
  // themselves. Everything is found and composed on the GPU, in the host's
  // command buffer.
  kZero = 0,
  // ReallyZero, the economy engine: a dense optical flow solved at half
  // resolution, a midpoint synthesis and a cleanup where the flow doubts.
  kReallyZero = 1,
  // The same engines by their development names.
  kRC7 = kZero,
  kDenseFlow = kReallyZero,
};

// Algorithm and execution backend are independent axes: every backend
// produces the same logical result.
enum class Backend : uint8_t {
  kAuto = 0,
  kCompat,
  kModern,
  kQcom,
};

struct ActiveRect {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct VulkanContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;

  // ZeroFG resolves Vulkan entry points through the host loader so custom
  // ICDs and drivers use exactly the same Vulkan dispatch path as the host.
  PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;

  const VkAllocationCallbacks* allocator = nullptr;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0;
  uint32_t height = 0;
  VkImageUsageFlags usage = 0;
  // Optional backend qualification uses real creation facts; unknown callers
  // retain Compat rather than qualifying arbitrary imports by assumption.
  VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
  VkImageCreateFlags create_flags = 0;
  VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
  bool creation_facts_known = false;

  // The physical image may contain padding or a letterboxed allocation.  A
  // zero-sized active rect means the complete physical image.
  ActiveRect active_rect;

  // Identity of the Real this image holds in the host's accepted order (any
  // strictly increasing counter; 0 = unknown). ZeroFG reuses the Real it
  // prepared only when the pair it is about to record starts at the Real the
  // previous recorded pair ended on, i.e. when previous.sequence equals the
  // current.sequence of a pair this Interpolator recorded; a gap, a reset, a
  // changed geometry or an unknown (0) sequence prepare it again. The motion
  // it starts from is a candidate, never a reused result: a known gap (both
  // sequences nonzero and not continuous) drops it, an unknown sequence keeps
  // it as a candidate that loses where it does not fit. The same pair again
  // (both sequences equal to the last call's) is another phase of it: see
  // Interpolate.
  uint64_t sequence = 0;
};

struct CreateInfo {
  VulkanContext vulkan;

  Mode mode = Mode::kZero;
  Backend backend = Backend::kAuto;
  Capabilities capabilities;

  // Number of independent GPU resource contexts used for
  // frames in flight.
  uint32_t frame_context_count = 3;

  // Zero's development switches (Mode::kZero only; ZeroOption bits), fixed
  // for the Interpolator's life: the host makes a new one to change them.
  uint32_t zero_options = 0;
};

// CreateInfo::zero_options (slimming, 2026-11-18; lab docs/ZERO_SLIM_2026-11.md).
enum ZeroOption : uint32_t {
  // Zero light: one background surface, one render pass, the lattice's
  // mattes without the guided filter. The render about 40 % cheaper on the
  // Adreno 840; the same or better on the real clips, weaker where several
  // rigid surfaces cross.
  kZeroLight = 1u,
  // MFG: the phases away from the middle (outside 0.3-0.7: x4's quarters)
  // show the dense-flow base's picture; the layers' picture only near the
  // middle. The pair's decisions are still made on its first record.
  kZeroQuarterBase = 2u,
};

class AlgorithmContext;

class Interpolator {
 public:
  static std::unique_ptr<Interpolator> Create(
      const CreateInfo& create_info,
      Status* status = nullptr);

  ~Interpolator();

  Interpolator(const Interpolator&) = delete;
  Interpolator& operator=(const Interpolator&) = delete;

  // Allocates/reallocates the internal working resources.
  // This should be called outside the hot presentation path whenever possible.
  Status Resize(uint32_t width,
                uint32_t height,
                VkFormat input_format,
                VkFormat output_format);

  // Records commands that generate a temporal frame between previous and
  // current.
  //
  // phase:
  //   0.0 = previous
  //   0.5 = midpoint (the ZeroFG 2x target)
  //   1.0 = current
  // Any phase strictly between 0 and 1 (x3: 1/3, 2/3; x4: 0.25, 0.5, 0.75).
  // A call with the pair of the last successful call (previous.sequence and
  // current.sequence nonzero and equal to that call's, the same geometry),
  // when this call or that one is off the middle, records only what depends
  // on the phase: the per-pair work is that call's. Two middles of one pair
  // are two whole records, as at x2 always.
  // Record the phases of a pair one after another on the same queue, before
  // any other pair, each with its own frame_context_index (frame_context_count
  // at least N + 1 for xN), and keep both Reals alive until the last one
  // completes. With sequence 0 every call is a full record.
  //
  // previous/current must have VK_IMAGE_USAGE_SAMPLED_BIT.
  // output must be distinct. The chosen backend requires either storage-image
  // output support or VK_IMAGE_USAGE_TRANSFER_DST_BIT for its format fallback.
  //
  // The command buffer must support compute and, when the format fallback is
  // selected, transfer/blit operations.
  // frame_context_index selects an independent GPU resource context.
  // The caller must not reuse the same index until the GPU submission
  // containing the previous Interpolate call for that index has completed.
  Status Interpolate(VkCommandBuffer command_buffer,
                     uint32_t frame_context_index,
                     const Image& previous,
                     const Image& current,
                     float phase,
                     const Image& output);

  // The HUD of one Real, as a host that sees the draws can give it (ARMSX2's
  // ZeroGS, from the PS2's GS): every HUD draw drawn a second time,
  // with the game's own blend, into a plane cleared to black (p) and one
  // cleared to white (w). Every blend is out = P + under * Q, so p is what the
  // HUD adds and w - p how much of the picture under it still shows (per
  // channel). Both at the Real's size (their own active rects may place them
  // anywhere in their images), sampled, colour 0..1.
  struct HudPlanes {
    Image p;
    Image w;
  };

  // The same as above, with the HUD planes of both Reals. The HUD that is the
  // same in both Reals and that both pairs of planes explain is taken out of
  // the Reals, the engine runs once on them, and current's HUD is put back on
  // exactly those pixels; anything else (a HUD that moves or changes, a pixel
  // something was drawn over) is interpolated as always. A plane with no
  // image, a Real that is not A2B10G10R10, or any plane that does not fit
  // makes this exactly the call above: the HUD layer never fails a frame. The
  // engine gets the HUD-free pictures with sequence 0. Zero also hears where
  // the layer left a HUD that stays in place (digits changing on a panel) and
  // adds it to its own screen, so it never moves pieces of it. The planes are
  // used at the middle (0.5) only: any other phase is the call above.
  Status Interpolate(VkCommandBuffer command_buffer,
                     uint32_t frame_context_index,
                     const Image& previous,
                     const Image& current,
                     float phase,
                     const Image& output,
                     const HudPlanes& previous_hud,
                     const HudPlanes& current_hud);

 private:
  Interpolator(
      std::vector<std::unique_ptr<AlgorithmContext>> contexts,
      uint32_t frame_context_count,
      const CreateInfo& create_info);

  std::vector<std::unique_ptr<AlgorithmContext>> contexts_;
  uint32_t frame_context_count_ = 0;
  // The HUD layer: made on the first call that brings HUD planes, at the size
  // and formats of the last Resize.
  CreateInfo create_info_;
  std::unique_ptr<class HudLayer> hud_;
  bool hud_failed_ = false;
  uint32_t width_ = 0, height_ = 0;
  VkFormat input_format_ = VK_FORMAT_UNDEFINED, output_format_ = VK_FORMAT_UNDEFINED;
  // The pair of the last successful plain record (MFG: a call with the same pair records only the new phase).
  static bool SameGeometry(const Image& a, const Image& b);
  bool pair_valid_ = false;
  uint64_t pair_previous_ = 0, pair_current_ = 0;
  float pair_phase_ = 0.5f;
  Image pair_geometry_[2];
};

}  // namespace zerofg
