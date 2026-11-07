// RC7 on the GPU (the raw form, lab only): the layers of ComposeLayers, pass by pass. Plans are storage buffers on the lattice; the Reals and their softened copies are sampled.
// Positions are pixels with the centre of pixel i at i + 0.5, as in the CPU form. Side 0 is A, side 1 is B.

// A uniform block, not a storage buffer: every pass reads these all the time, and a driver can keep a uniform block in constants, with no trip to memory.
layout(std140, binding = 0) uniform Params {
  ivec4 dim;     // w, h, lw, lh
  ivec4 cfg;     // s, planes, radius, front_first
  ivec4 cfg2;    // fw, fh, flow_w, flow_h
  vec4 cell;     // cw, ch, flow_scale, soft_r
  vec4 gate;     // gate a, gate b, tau of the soft labels a, b
  vec4 th0;      // tex_bar, surf_bar, explain_share, margin
  vec4 th1;      // flat share, ghost, stand_min, laydraw share (0 off)
  vec4 th2;      // laycover share, laysoft, laysmooth tolerance, laycons limit
  vec4 th3;      // laybase share (0 off), layhudmove, laytwo, layback
  vec4 judge0;   // the picture judge: contradiction share, bar share, price of a lone sample, patch radius in pixels
  vec4 judge1;   // its margin, the fine step's window in pixels, the guided mattes' window in pixels (0 off), their epsilon squared
  vec4 th4;      // layhudrel share (0 off), layhudskip, laytile, laypat5
  ivec4 cfg3;    // where the flow starts in its buffer, 1 when the pair is a cut, layunfold, layhudmem
  ivec4 order;   // the surfaces, front to back: 4 in here, 2 in order2
  ivec4 order2;
  vec4 H[18];    // A to B per surface, 3 rows, normalised coordinates
  vec4 Hi[18];   // B to A
  vec4 Mid[18];  // middle to A (the square root's inverse)
} u;

layout(std430, binding = 1) buffer CostBuf { float cost[]; };      // [kind 0 sharp, 1 soft][side][surface][point]
layout(std430, binding = 2) buffer LabelBuf { uint label[]; };     // [flip][side][point]
layout(std430, binding = 3) buffer PlanBuf { float plan[]; };      // [what][side][point]: see the P_ names
layout(std430, binding = 4) buffer StatBuf { float stat[]; };      // [what][side][point]: 0 texture, 1 soft texture, 2 what a wrong match costs (soft)
layout(std430, binding = 5) buffer FrontBuf { vec2 front[]; };     // [flip][side][point]
layout(std430, binding = 6) buffer AlphaBuf { float palpha[]; };   // [side][surface][point]
layout(std430, binding = 7) buffer CellBuf { vec4 cells[]; };      // per cell: x y of the cell's vector, of the motion around it
layout(std430, binding = 8) buffer OutBuf { uint out_words[]; };
#ifdef ZFG_RC7_CORE
layout(binding = 9) uniform sampler2D basePic;                     // the RC5 picture
#else
layout(std430, binding = 9) buffer PicBuf { uint pics[]; };        // the base picture, then the dense one
#endif
layout(std430, binding = 10) buffer DisBuf { float disagree[]; };  // per pixel
layout(binding = 11) uniform sampler2D realA;
layout(binding = 12) uniform sampler2D realB;
layout(binding = 13) uniform sampler2D softA;
layout(binding = 14) uniform sampler2D softB;
layout(binding = 15, rgba8) uniform writeonly image2D softOutA;
layout(binding = 16, rgba8) uniform writeonly image2D softOutB;
layout(std430, binding = 17) buffer FlowBuf { vec2 dense[]; };     // A to B, then B to A, per point of the flow level
layout(std430, binding = 18) buffer CountBuf { uint counter[]; };
layout(std430, binding = 19) buffer BlockBuf { vec4 blocks[]; };   // [side][block]: the mean colour of what is not raw, its weight
layout(std430, binding = 20) buffer JudgeBuf { vec4 judge[]; };    // [flip][picture][grid point]: the best vector for both, their summed cost, the best for either alone
layout(std430, binding = 21) buffer JPicBuf { vec4 jpic[]; };      // [picture][point]: the finished pictures, softened
// Read through the hardware's bilinear, one fetch instead of four reads and a hand mix.
layout(binding = 22) uniform sampler2DArray bendTex;               // [side * surfaces + surface] on a coarse grid: the surface's last fraction of a pixel
layout(binding = 23) uniform sampler2DArray guideTex;              // [side * (surfaces + 1) + front, then the surfaces] on the lattice: the guided filter's slope and offset
layout(binding = 34, rg16f) uniform writeonly image2DArray guideOut;
layout(binding = 35, rg16f) uniform image2DArray bendOut;
layout(std430, binding = 24) buffer TileBuf { uint tiles[]; };     // [marked, widened][block]: which layers exist there
layout(binding = 28, rgb10_a2) uniform writeonly image2D halfOutA;  // the half-size pictures the decisions read when the Reals are large
layout(binding = 29, rgb10_a2) uniform writeonly image2D halfOutB;
layout(std430, binding = 32) buffer SnapBuf { vec4 snap[]; };      // the cells as they were when the search under way started
layout(binding = 30) uniform sampler2D cubicA;                     // the Reals through the hardware's cubic filter, when it has one: only the render reads them
layout(binding = 31) uniform sampler2D cubicB;
// The matte's points of each picture, x | y << 16, and the dispatches over them; after them, the picture's groups by class (one list per surface, then everything else), and theirs.
layout(std430, binding = 27) buffer FrontListBuf { uvec4 f_args[2]; uint f_count[2]; uint f_pad[2]; uvec4 r_args[7]; uint r_count[7]; uint r_pad; uint f_list[]; };

// Which picture a pipeline works on, known when it is built. A branch costs as much as 15 to 40 instructions on the Adreno, so picking the Real with one at every fetch cost more than the arithmetic of the sample.
layout(constant_id = 0) const int THIS_SIDE = 0;
layout(constant_id = 1) const int THIS_KIND = 0;
// What the render knows when it is built: the one surface of this class of groups (-1 the full path), the fixed points' rounds, and the options: 1 blocks, 2 behind, 4 two passes, 8 the screen's hole, 16 guided mattes,
// 32 the hardware's cubic, 64 the front always first, 128 debug pictures, 256 the groups come from a list, 512 the costs over a checkerboard of the patch, 1024 the statistics too, 2048 the guided window sparser, 4096 the front's patch over a checkerboard, 8192 standing out too, 16384 the fine step's window sparser, 32768 the matte's squares sparser.
layout(constant_id = 2) const int R_SURFACE = -1;
layout(constant_id = 3) const int R_BEND = 1;
layout(constant_id = 4) const int R_FRONT = 2;
layout(constant_id = 5) const int R_OPTS = 0;
// Exact core delivery/reuse: 8 fused mix/output, 512 sharp-only A statistics
// when their source-only soft statistics were retained from the former B.
layout(constant_id = 6) const int REUSE_OPTS = 0;

layout(push_constant) uniform Push {
  ivec4 a;
  ivec4 b;
  vec4 p;
} pc;

#define W u.dim.x
#define H_ u.dim.y
#define LW u.dim.z
#define LH u.dim.w
#define S u.cfg.x
#define PLANES u.cfg.y
#define TILE 16
int tileW() { return (W + TILE - 1) / TILE; }
int tileH() { return (H_ + TILE - 1) / TILE; }

// What the plan buffer holds.
const int P_BG = 0;       // the best sharp cost over the surfaces, -1 where one of them is blind
const int P_SOFT = 1;     // the best soft cost
const int P_DIST0 = 2;    // the labels' geodesic distance, two flips
const int P_DIST1 = 3;
const int P_MASK = 4;     // the front's matte
const int P_TMP = 5;      // scratch
const int P_STAND = 6;    // how much a point stands out from the background around it
const int P_CONS = 7;     // the vector comes back from the other picture
const int P_FCOST0 = 8;   // the front's cost, two flips
const int P_FCOST1 = 9;
const int P_ALPHA = 10;   // the soft matte
const int P_HUD = 11;     // the screen (side 0 only is used)
const int P_RAW = 12;     // the raw matte
const int P_TRUST = 13;   // how much the judge gives back to the base
const int P_JFLAG = 14;   // the layer contradicts itself here: the judge asks
const int P_JC1 = 15;     // what the base took
const int P_JC2 = 16;     // what the dense picture took
const int P_PRESENT = 17;  // which mattes are not empty here: bit 0 the front, bit 1 + k the surface k
const int P_COUNT = 18;

const int BEND_STEP = 4;  // lattice points per point of the coarse grid of the bends
int ln() { return LW * LH; }
// The patch, in lattice steps: known when the shader is compiled, so a loop over it unrolls and the arrays it fills are gone. An array a lane indexes with a value it computes costs far more than one it does not.
const ivec2 IPATCH[16] = ivec2[16](ivec2(-3, -3), ivec2(-1, -3), ivec2(1, -3), ivec2(3, -3), ivec2(-3, -1), ivec2(-1, -1), ivec2(1, -1), ivec2(3, -1), ivec2(-3, 1), ivec2(-1, 1), ivec2(1, 1), ivec2(3, 1), ivec2(-3, 3), ivec2(-1, 3), ivec2(1, 3), ivec2(3, 3));
const int THIN[8] = int[8](0, 2, 5, 7, 8, 10, 13, 15);
const vec2 PATCH[16] = vec2[16](vec2(-3, -3), vec2(-1, -3), vec2(1, -3), vec2(3, -3), vec2(-3, -1), vec2(-1, -1), vec2(1, -1), vec2(3, -1), vec2(-3, 1), vec2(-1, 1), vec2(1, 1), vec2(3, 1), vec2(-3, 3), vec2(-1, 3), vec2(1, 3), vec2(3, 3));
int planOf(int what, int side, int p) { return (what * 2 + side) * ln() + p; }
int statOf(int what, int side, int p) { return (what * 2 + side) * ln() + p; }
int costOf(int kind, int side, int k, int p) { return ((kind * 2 + side) * PLANES + k) * ln() + p; }
int labelOf(int flip, int side, int p) { return (flip * 2 + side) * ln() + p; }
int frontOf(int flip, int side, int p) { return (flip * 2 + side) * ln() + p; }
int alphaOf(int side, int k, int p) { return (side * PLANES + k) * ln() + p; }

vec2 latCentre(ivec2 q) { return (vec2(q) + 0.5) * float(S); }
vec2 sizeF() { return vec2(float(W), float(H_)); }
vec3 sampleReal(int side, vec2 pos) { return (side == 0 ? textureLod(realA, pos / sizeF(), 0.0) : textureLod(realB, pos / sizeF(), 0.0)).rgb; }
vec3 sampleSoft(int side, vec2 pos) { return (side == 0 ? textureLod(softA, pos / sizeF(), 0.0) : textureLod(softB, pos / sizeF(), 0.0)).rgb; }
float l1(vec3 a, vec3 b) { vec3 d = abs(a - b); return (d.x + d.y + d.z) / 3.0; }

vec2 homo(vec4 r0, vec4 r1, vec4 r2, vec2 pos) {
  vec2 n = pos * 2.0 / sizeF() - 1.0;
  float w_ = r2.x * n.x + r2.y * n.y + r2.z;
  vec2 q = vec2(r0.x * n.x + r0.y * n.y + r0.z, r1.x * n.x + r1.y * n.y + r1.z) / w_;
  return (q + 1.0) * sizeF() * 0.5;
}
vec2 surfaceGo(int side, int k, vec2 pos) {
  int t = k * 3;
  return side == 0 ? homo(u.H[t], u.H[t + 1], u.H[t + 2], pos) : homo(u.Hi[t], u.Hi[t + 1], u.Hi[t + 2], pos);
}
// From the middle to both pictures along a surface.
void surfaceMid(int k, vec2 pos, out vec2 pa, out vec2 pb) {
  int t = k * 3;
  pa = homo(u.Mid[t], u.Mid[t + 1], u.Mid[t + 2], pos);
  pb = homo(u.H[t], u.H[t + 1], u.H[t + 2], pa);
}
bool insideP(vec2 q) { return all(bvec4(q.x >= 0.0, q.y >= 0.0, q.x < float(W), q.y < float(H_))); }
bool insideM(vec2 q) { return all(bvec4(q.x >= 1.0, q.y >= 1.0, q.x <= float(W) - 1.0, q.y <= float(H_) - 1.0)); }
int latIndex(vec2 pos) {
  ivec2 q = clamp(ivec2(pos / float(S)), ivec2(0), ivec2(LW - 1, LH - 1));
  return q.y * LW + q.x;
}
// A plan bilinear on the lattice.
float planAt(int what, int side, vec2 pos) {
  vec2 g = clamp(pos / float(S) - 0.5, vec2(0.0), vec2(float(LW) - 1.001, float(LH) - 1.001));
  ivec2 i = ivec2(g);
  vec2 f = g - vec2(i);
  int p = i.y * LW + i.x;
  return mix(mix(plan[planOf(what, side, p)], plan[planOf(what, side, p + 1)], f.x), mix(plan[planOf(what, side, p + LW)], plan[planOf(what, side, p + LW + 1)], f.x), f.y);
}
float alphaAt(int side, int k, vec2 pos) {
  vec2 g = clamp(pos / float(S) - 0.5, vec2(0.0), vec2(float(LW) - 1.001, float(LH) - 1.001));
  ivec2 i = ivec2(g);
  vec2 f = g - vec2(i);
  int p = i.y * LW + i.x;
  return mix(mix(palpha[alphaOf(side, k, p)], palpha[alphaOf(side, k, p + 1)], f.x), mix(palpha[alphaOf(side, k, p + LW)], palpha[alphaOf(side, k, p + LW + 1)], f.x), f.y);
}
vec2 frontAt(int side, vec2 pos) {
  vec2 g = clamp(pos / float(S) - 0.5, vec2(0.0), vec2(float(LW) - 1.001, float(LH) - 1.001));
  ivec2 i = ivec2(g);
  vec2 f = g - vec2(i);
  int p = i.y * LW + i.x;
  return mix(mix(front[frontOf(0, side, p)], front[frontOf(0, side, p + 1)], f.x), mix(front[frontOf(0, side, p + LW)], front[frontOf(0, side, p + LW + 1)], f.x), f.y);
}
// A matte at a pixel: the guided filter's numbers with that very pixel's luma; or, with no guide, the plain soft matte. k = -1 is the front.
float matteAt(int side, int k, vec2 pos) {
  if ((R_OPTS & 16) == 0) return k < 0 ? planAt(P_ALPHA, side, pos) : alphaAt(side, k, pos);
  vec2 ab = textureLod(guideTex, vec3(pos / (float(S) * vec2(float(LW), float(LH))), float(side * (PLANES + 1) + k + 1)), 0.0).xy;
  return clamp(ab.x * dot(sampleReal(side, pos), vec3(0.2126, 0.7152, 0.0722)) + ab.y, 0.0, 1.0);
}
vec2 bendAt(int side, int k, vec2 pos) {
  int gw = max(1, LW / BEND_STEP), gh = max(1, LH / BEND_STEP);
  return textureLod(bendTex, vec3(pos / (float(S * BEND_STEP) * vec2(float(gw), float(gh))), float(side * PLANES + k)), 0.0).xy;
}
vec3 wordColour(uint word) { return vec3(float(word & 1023u), float((word >> 10) & 1023u), float((word >> 20) & 1023u)) / 1023.0; }
#ifdef ZFG_RC7_CORE
vec3 basePicture(ivec2 q) { return textureLod(basePic, (vec2(q) + 0.5) / sizeF(), 0.0).rgb; }
vec3 densePicture(ivec2 q) { return basePicture(q); }
#else
vec3 basePicture(ivec2 q) { return wordColour(pics[q.y * W + q.x]); }
vec3 densePicture(ivec2 q) { return wordColour(pics[W * H_ + q.y * W + q.x]); }
#endif
uint colourWord(vec3 c) {
  uvec3 v = uvec3(clamp(c, vec3(0.0), vec3(1.0)) * 1023.0 + 0.5);
  return (3u << 30) | v.x | (v.y << 10) | (v.z << 20);
}
