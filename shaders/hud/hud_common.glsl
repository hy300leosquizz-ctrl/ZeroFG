// The GS modes' HUD layer (src/hud/hud_layer.cc): what its three passes share. The host draws each HUD draw a second time into two side
// targets, one cleared to black (P) and one to white (W); every GS blend is out = P + dst * Q, so Q = W - P (per channel) is how much of
// the picture under the HUD still shows and the picture without the HUD is (Real - P) / Q where Q > 0. The laboratory form:
// docs/GS_HUD_RULE_2026-11-06.md and testbed/scripts/hud_layer_lab.py (keep_mask, inconsistent, dehud) on the algorithm repository.

layout(push_constant) uniform HudPush {
  ivec2 real_origin;  // the Reals' active rect origin
  ivec2 extent;       // the picture
  ivec2 p_a_origin;   // each plane's active rect origin
  ivec2 w_a_origin;
  ivec2 p_b_origin;
  ivec2 w_b_origin;
  int reuse_a;        // 1: world_a already holds A's picture from the pair before (hud_prep.comp)
} pc;

const float kQMin = 0.03;     // under this the HUD is opaque: no world under it
const float kChanged = 0.03;  // P or Q apart by more than this between A and B: the HUD moved or changed
const float kTol = 0.04;      // how far a Real may stray from what its planes explain
const float kHud = 0.05;      // 1 - Q above this (the mean of the channels): a HUD pixel

// The code image (r8, code / 255; the engine's screen too, AlgorithmContext::SetScreenTruth and shaders/rc7/rc7_hudgs.comp): a HUD pixel
// in A, in B, and one the layer takes out of both and puts back.
const uint kCodeA = 1u, kCodeB = 2u, kCodeKept = 4u;
uint Code(float v) {
  return uint(round(v * 255.0));
}

vec3 Q(vec3 p, vec3 w) {
  return clamp(w - p, 0.0, 1.0);
}

// The picture under the HUD, channel by channel: the Real itself where the HUD is opaque.
vec3 World(vec3 c, vec3 p, vec3 q) {
  return mix(c, clamp((c - p) / max(q, vec3(kQMin)), 0.0, 1.0), greaterThan(q, vec3(kQMin)));
}
