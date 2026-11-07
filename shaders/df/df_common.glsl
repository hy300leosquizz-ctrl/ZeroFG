// RC5 seed 2, the dense-flow frame generator (the `df` profile of the testbed, lab only). Shared declarations of its passes.
// Written from the published algorithms (DIS, Kroeger et al. 2016; the Super SloMo midpoint flow; softmax splatting, Niklaus & Liu 2020) and
// testbed/tools/densefg/df_pairs.cpp, the CPU form it mirrors. Every plane is a storage buffer: the prototype measures the algorithm, not a tuned layout.
// Pixel-index coordinates: the centre of pixel i is i (bilinear reads clamp to the plane).

layout(std430, binding = 0) buffer SrcBuf { uint src_words[]; };      // A then B, the full-resolution Reals (A2B10G10R10 or RGBA8)
layout(std430, binding = 1) buffer PyrBuf { float pyr[]; };            // luma pyramids of A and B (0..255)
layout(std430, binding = 2) buffer FlowBuf { vec2 flow[]; };           // dense flow per level and direction (0: A -> B, 1: B -> A)
layout(std430, binding = 3) buffer PatchBuf { vec2 pflow[]; };         // per-patch flow of the level being solved
layout(std430, binding = 4) buffer VrBuf { float vrs[]; };             // variational refinement scratch: Ix, Iy, It, du0, dv0, du1, dv1 per pixel
layout(std430, binding = 5) buffer AccBuf { uint acc[]; };             // splat accumulators: R, G, B, W per full-resolution pixel (fixed point)
layout(std430, binding = 6) buffer MidBuf { vec4 mid[]; };             // the middle picture before the cleanup (rgb) and the doubt indicator (w)
layout(std430, binding = 7) buffer DensBuf { float dens[]; };          // the density of doubtful pixels
layout(std430, binding = 8) buffer OutBuf { uint out_words[]; };       // the output, A2B10G10R10
layout(std430, binding = 19) buffer MidWBuf { uint midw[]; };          // the compact middle picture: A2B10G10R10 words, the doubt indicator in bit 30

// The luma pyramid as R16F textures (one descriptor set per level): the readers sample with the hardware bilinear filter (clamp to edge), the
// writers store through the storage views of the same images.
layout(binding = 9) uniform sampler2D texA;
layout(binding = 10) uniform sampler2D texB;
layout(binding = 11, r16f) uniform writeonly image2D outA;
layout(binding = 12, r16f) uniform writeonly image2D outB;

// The Reals themselves (0..1 rgb through the hardware bilinear filter), and the finest solved level's flow per direction as RG16F textures (the dense
// pass, or the refinement after it, writes them through their storage views): the `tx` form of the final pass reads these instead of the buffers.
layout(binding = 13) uniform sampler2D realA;
layout(binding = 14) uniform sampler2D realB;
layout(binding = 15) uniform sampler2D flowT0;
layout(binding = 16) uniform sampler2D flowT1;
layout(binding = 17, rg16f) uniform writeonly image2D flowO0;
layout(binding = 18, rg16f) uniform writeonly image2D flowO1;

// The mobile patch search (df_patch_t): per level, I0's luma and its central differences packed (T, gx, gy) in an RGBA16F texture per Real
// (df_grad writes them through the storage views), so a patch pixel costs one fetch instead of five.
layout(binding = 21) uniform sampler2D gradA;
layout(binding = 22) uniform sampler2D gradB;
layout(binding = 23, rgba16f) uniform writeonly image2D outGA;
layout(binding = 24, rgba16f) uniform writeonly image2D outGB;

#ifdef ZFG_DF_IMAGE_OUT
layout(binding = 20, rgb10_a2) uniform writeonly image2D out_image;  // the ZeroFG core: the output picture (or its internal copy)
#endif
// The cleanup only around doubts: a flag per tile, then the list of the tiles with a flag in their 3 x 3, and the dispatch over them.
layout(std430, binding = 25) buffer CleanBuf { uvec4 c_args; uint c_count; uint c_pad[3]; uint c_corr[6]; int c_cut; uint c_pad2; uint c_words[]; };

layout(push_constant) uniform Push {
  ivec4 a;
  ivec4 b;
  ivec4 c;
  vec4 p;
} pc;

// Known when the pipeline is built: the direction (0 A to B), and the options of a pass (the final pass: bits as in df_final; the densification: the weight's exponent; the pyramid: 1 also writes the buffers). A branch or a
// size query at every fetch costs as much as the fetch on the Adreno.
layout(constant_id = 0) const int DF_DIR = 0;
layout(constant_id = 1) const int DF_OPTS = 0;
// Exact standalone-product optimizations: 1 direct args, 2 grouped cut,
// 4 lazy sharp colour, 8 identity cleanup stores, 16 texture-only fine flow.
// DenseFlowSource consumers leave these off, retaining their FP32 flow buffer.
layout(constant_id = 2) const int DF_PRODUCT_OPTS = 0;
#ifndef DF_LEVEL_SIZE
#define DF_LEVEL_SIZE(T) textureSize(T, 0)
#endif
#ifndef DF_REAL_SIZE
#define DF_REAL_SIZE(T) textureSize(T, 0)
#endif
#ifndef DF_FLOW_SIZE
#define DF_FLOW_SIZE(T) textureSize(T, 0)
#endif

float plane_at(int off, int w, int h, int x, int y) {
  return pyr[off + clamp(y, 0, h - 1) * w + clamp(x, 0, w - 1)];
}

float plane_bil(int off, int w, int h, float x, float y) {
  x = clamp(x, 0.0, float(w - 1));
  y = clamp(y, 0.0, float(h - 1));
  int x0 = int(x), y0 = int(y);
  int x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
  float ux = x - float(x0), uy = y - float(y0);
  float v00 = pyr[off + y0 * w + x0], v10 = pyr[off + y0 * w + x1];
  float v01 = pyr[off + y1 * w + x0], v11 = pyr[off + y1 * w + x1];
  return mix(mix(v00, v10, ux), mix(v01, v11, ux), uy);
}

vec2 flow_bil(int off, int w, int h, float x, float y) {
  x = clamp(x, 0.0, float(w - 1));
  y = clamp(y, 0.0, float(h - 1));
  int x0 = int(x), y0 = int(y);
  int x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
  float ux = x - float(x0), uy = y - float(y0);
  vec2 v00 = flow[off + y0 * w + x0], v10 = flow[off + y0 * w + x1];
  vec2 v01 = flow[off + y1 * w + x0], v11 = flow[off + y1 * w + x1];
  return mix(mix(v00, v10, ux), mix(v01, v11, ux), uy);
}

// The pyramid textures of the bound level: b_side picks B (else A). Pixel-index coordinates, as plane_bil / plane_at.
float tex_bil(bool b_side, float x, float y) {
  if (b_side) return textureLod(texB, (vec2(x, y) + 0.5) / vec2(DF_LEVEL_SIZE(texB)), 0.0).r;
  return textureLod(texA, (vec2(x, y) + 0.5) / vec2(DF_LEVEL_SIZE(texA)), 0.0).r;
}

float tex_at(bool b_side, int x, int y) {
  if (b_side) return texelFetch(texB, clamp(ivec2(x, y), ivec2(0), DF_LEVEL_SIZE(texB) - 1), 0).r;
  return texelFetch(texA, clamp(ivec2(x, y), ivec2(0), DF_LEVEL_SIZE(texA) - 1), 0).r;
}

// The colour of a source word, 0..1.
vec3 decode_word(uint w, bool ten_bit) {
  if (ten_bit) return vec3(float(w & 1023u), float((w >> 10) & 1023u), float((w >> 20) & 1023u)) / 1023.0;
  return vec3(float(w & 255u), float((w >> 8) & 255u), float((w >> 16) & 255u)) / 255.0;
}

vec3 src_at(int frame, int W, int H, int x, int y, bool ten_bit) {
  return decode_word(src_words[frame * W * H + clamp(y, 0, H - 1) * W + clamp(x, 0, W - 1)], ten_bit);
}

vec3 src_bil(int frame, int W, int H, float x, float y, bool ten_bit) {
  x = clamp(x, 0.0, float(W - 1));
  y = clamp(y, 0.0, float(H - 1));
  int x0 = int(x), y0 = int(y);
  float ux = x - float(x0), uy = y - float(y0);
  vec3 v00 = src_at(frame, W, H, x0, y0, ten_bit), v10 = src_at(frame, W, H, x0 + 1, y0, ten_bit);
  vec3 v01 = src_at(frame, W, H, x0, y0 + 1, ten_bit), v11 = src_at(frame, W, H, x0 + 1, y0 + 1, ten_bit);
  return mix(mix(v00, v10, ux), mix(v01, v11, ux), uy);
}

// The full-resolution flow of a direction, from its finest solved level (the CPU form's repeated x2 upsampling, collapsed).
vec2 full_flow(int off, int wf, int hf, int scale, float x, float y) {
  return float(scale) * flow_bil(off, wf, hf, x / float(scale), y / float(scale));
}

// The texture forms of src_bil and full_flow (the same pixel-index coordinates; clamp to edge = the buffers' clamped reads).
vec3 real_bil(int frame, float x, float y) {
  if (frame != 0) return textureLod(realB, (vec2(x, y) + 0.5) / vec2(DF_REAL_SIZE(realB)), 0.0).rgb;
  return textureLod(realA, (vec2(x, y) + 0.5) / vec2(DF_REAL_SIZE(realA)), 0.0).rgb;
}

vec2 full_flow_t(int d, int scale, float x, float y) {
  vec2 q = vec2(x, y) / float(scale) + 0.5;
  if (d != 0) return float(scale) * textureLod(flowT1, q / vec2(DF_FLOW_SIZE(flowT1)), 0.0).rg;
  return float(scale) * textureLod(flowT0, q / vec2(DF_FLOW_SIZE(flowT0)), 0.0).rg;
}

// The sharp colour: Catmull-Rom over the 4 x 4 texels around the sample, in nine bilinear taps (the outer pairs of weights folded into one tap
// each axis), clamped to the range of the 2 x 2 texels around the sample so a hard edge cannot ring. The same kernel as the RC3 resolve's
// Compat sharp form, written from the spline's weights.
vec3 real_sharp(int frame, float x, float y) {
  vec2 size = frame != 0 ? vec2(DF_REAL_SIZE(realB)) : vec2(DF_REAL_SIZE(realA));
  vec2 p = vec2(x, y) + 0.5;            // texel-space position (texel centres at k + 0.5)
  vec2 t1 = floor(p - 0.5) + 0.5;       // the centre of the texel up-left of the sample
  vec2 f = p - t1;
  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  vec2 w3 = f * f * (-0.5 + 0.5 * f);
  vec2 w12 = w1 + w2;
  vec2 c0 = (t1 - 1.0) / size, c3 = (t1 + 2.0) / size, c12 = (t1 + w2 / w12) / size;
  vec3 s = vec3(0.0);
  vec2 wx = vec2(0.0);
  vec2 cx[3] = vec2[3](vec2(c0.x, w0.x), vec2(c12.x, w12.x), vec2(c3.x, w3.x));
  vec2 cy[3] = vec2[3](vec2(c0.y, w0.y), vec2(c12.y, w12.y), vec2(c3.y, w3.y));
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) {
      vec2 uv = vec2(cx[i].x, cy[j].x);
      vec3 v = frame != 0 ? textureLod(realB, uv, 0.0).rgb : textureLod(realA, uv, 0.0).rgb;
      s += cx[i].y * cy[j].y * v;
    }
  ivec2 b = ivec2(floor(p - 0.5));
  ivec2 hi = ivec2(size) - 1;
  vec3 a00, a10, a01, a11;
  if (frame != 0) {
    a00 = texelFetch(realB, clamp(b, ivec2(0), hi), 0).rgb; a10 = texelFetch(realB, clamp(b + ivec2(1, 0), ivec2(0), hi), 0).rgb;
    a01 = texelFetch(realB, clamp(b + ivec2(0, 1), ivec2(0), hi), 0).rgb; a11 = texelFetch(realB, clamp(b + ivec2(1, 1), ivec2(0), hi), 0).rgb;
  } else {
    a00 = texelFetch(realA, clamp(b, ivec2(0), hi), 0).rgb; a10 = texelFetch(realA, clamp(b + ivec2(1, 0), ivec2(0), hi), 0).rgb;
    a01 = texelFetch(realA, clamp(b + ivec2(0, 1), ivec2(0), hi), 0).rgb; a11 = texelFetch(realA, clamp(b + ivec2(1, 1), ivec2(0), hi), 0).rgb;
  }
  return clamp(s, min(min(a00, a10), min(a01, a11)), max(max(a00, a10), max(a01, a11)));
}

// The cheaper sharp colour: the same Catmull-Rom weights on the five taps of the centre cross (the four corner taps of real_sharp's nine
// dropped, the rest renormalised), clamped the same way. 9 fetches instead of 13.
vec3 real_sharp5(int frame, float x, float y) {
  vec2 size = frame != 0 ? vec2(DF_REAL_SIZE(realB)) : vec2(DF_REAL_SIZE(realA));
  vec2 p = vec2(x, y) + 0.5;
  vec2 t1 = floor(p - 0.5) + 0.5;
  vec2 f = p - t1;
  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  vec2 w3 = f * f * (-0.5 + 0.5 * f);
  vec2 w12 = w1 + w2;
  vec2 c0 = (t1 - 1.0) / size, c3 = (t1 + 2.0) / size, c12 = (t1 + w2 / w12) / size;
  float wc = w12.x * w12.y, wl = w0.x * w12.y, wr = w3.x * w12.y, wt = w12.x * w0.y, wb = w12.x * w3.y;
  vec3 s;
  if (frame != 0)
    s = wc * textureLod(realB, c12, 0.0).rgb + wl * textureLod(realB, vec2(c0.x, c12.y), 0.0).rgb + wr * textureLod(realB, vec2(c3.x, c12.y), 0.0).rgb +
        wt * textureLod(realB, vec2(c12.x, c0.y), 0.0).rgb + wb * textureLod(realB, vec2(c12.x, c3.y), 0.0).rgb;
  else
    s = wc * textureLod(realA, c12, 0.0).rgb + wl * textureLod(realA, vec2(c0.x, c12.y), 0.0).rgb + wr * textureLod(realA, vec2(c3.x, c12.y), 0.0).rgb +
        wt * textureLod(realA, vec2(c12.x, c0.y), 0.0).rgb + wb * textureLod(realA, vec2(c12.x, c3.y), 0.0).rgb;
  s /= (wc + wl + wr + wt + wb);
  ivec2 b = ivec2(floor(p - 0.5));
  ivec2 hi = ivec2(size) - 1;
  vec3 a00, a10, a01, a11;
  if (frame != 0) {
    a00 = texelFetch(realB, clamp(b, ivec2(0), hi), 0).rgb; a10 = texelFetch(realB, clamp(b + ivec2(1, 0), ivec2(0), hi), 0).rgb;
    a01 = texelFetch(realB, clamp(b + ivec2(0, 1), ivec2(0), hi), 0).rgb; a11 = texelFetch(realB, clamp(b + ivec2(1, 1), ivec2(0), hi), 0).rgb;
  } else {
    a00 = texelFetch(realA, clamp(b, ivec2(0), hi), 0).rgb; a10 = texelFetch(realA, clamp(b + ivec2(1, 0), ivec2(0), hi), 0).rgb;
    a01 = texelFetch(realA, clamp(b + ivec2(0, 1), ivec2(0), hi), 0).rgb; a11 = texelFetch(realA, clamp(b + ivec2(1, 1), ivec2(0), hi), 0).rgb;
  }
  return clamp(s, min(min(a00, a10), min(a01, a11)), max(max(a00, a10), max(a01, a11)));
}

vec3 grad_at(bool b_side, int x, int y) {
  if (b_side) return texelFetch(gradB, clamp(ivec2(x, y), ivec2(0), textureSize(gradB, 0) - 1), 0).xyz;
  return texelFetch(gradA, clamp(ivec2(x, y), ivec2(0), textureSize(gradA, 0) - 1), 0).xyz;
}

void store_flow_t(int d, int x, int y, vec2 f) {
  if (d != 0) imageStore(flowO1, ivec2(x, y), vec4(f, 0.0, 0.0));
  else imageStore(flowO0, ivec2(x, y), vec4(f, 0.0, 0.0));
}
