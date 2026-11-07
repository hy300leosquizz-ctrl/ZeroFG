// The surfaces' search on the GPU: wide passes with short loops. Nothing here waits for the CPU, and the later passes get what it finds by a copy over Params.
layout(std430, binding = 25) buffer SearchBuf {
  ivec4 s_order;     // these first, in Params' own order: they are copied over it
  ivec4 s_order2;
  vec4 s_H[18];
  vec4 s_Hi[18];
  vec4 s_Mid[18];
  vec4 s_gate;
  int s_planes;      // copied over cfg.y
  int s_found;
  int s_stop;
  int s_inliers;
  int s_sample_n;
  int s_cur_plane;
  int s_pad0;
  int s_pad1;
  int s_size[8];
  int s_detail[8];
  int s_same[8];
  vec4 s_cur[3];     // the current fit, rows of the homography
  uint s_rest[128];  // a bit per cell still to explain
  uint s_claim[128]; // a bit per cell the current fit claims
  vec4 s_sample[256]; // an even sample of what is left: x y u v, normalised
  vec4 s_hyp[432];   // 144 trials: rows, the score in row 2's w
  vec4 s_acc[44];    // the fit's sums, 11 per group of 64 samples
  uint s_hist[256];  // the gates: [what][bin]
  int s_before[129]; // how many cells are left before each word of s_rest, and the total
  vec4 r_H[18];      // the last pair's surfaces at this pair's motion: copied over Params
  vec4 r_Hi[18];
  vec4 r_Mid[18];
  int r_sum[72];     // [surface][12]: what the cells say about each
  vec4 t_H[18];      // the surfaces being followed from pair to pair
  uint s_corr[6];    // sums over a sparse grid of the softened pictures: luma of A and of B, their squares and product, the count
  int s_cut;         // 1: the two pictures do not correlate at all: copied over cfg3.y
};

const int HYP_AFFINE = 96;
const int HYP_PLANE = 48;
const float AGREE_PX = 3.0;

vec2 searchScale() { return vec2(2.0 / float(W), 2.0 / float(H_)); }
vec2 hApply(vec4 r0, vec4 r1, vec4 r2, vec2 n) {
  vec3 p = vec3(n, 1.0);
  return vec2(dot(r0.xyz, p), dot(r1.xyz, p)) / dot(r2.xyz, p);
}
int cellCount() { return u.cfg2.x * u.cfg2.y; }

// A surface much like one already found adds nothing; nor does the echo of one.
bool alreadyKnown() {
  vec2 half_size = vec2(float(W), float(H_)) * 0.5;
  for (int k = 0; k < s_found; ++k) {
    float far_d = 0.0;
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x) {
        vec2 n = vec2(float(x), float(y)) * 0.8;
        far_d = max(far_d, length((hApply(s_cur[0], s_cur[1], s_cur[2], n) - hApply(s_H[k * 3], s_H[k * 3 + 1], s_H[k * 3 + 2], n)) * half_size));
      }
    if (far_d < 2.0) return true;
    if (s_detail[k] > 0 && float(s_same[k]) > 0.6 * float(s_detail[k])) return true;
  }
  return false;
}

// The current fit becomes a surface: its inverse, and the map from the middle, by the matrix square root (Denman-Beavers; Z ends as the inverse of the root).
void keepSurface(int size_px) {
  mat3 h = transpose(mat3(s_cur[0].xyz, s_cur[1].xyz, s_cur[2].xyz));
  mat3 hi = inverse(h);
  mat3 y = h, z = mat3(1.0);
  for (int it = 0; it < 8; ++it) {
    mat3 iy = inverse(y), iz = inverse(z);
    y = 0.5 * (y + iz);
    z = 0.5 * (z + iy);
  }
  int t = s_found * 3;
  mat3 ht = transpose(h), hit = transpose(hi), zt = transpose(z);
  for (int r = 0; r < 3; ++r) {
    s_H[t + r] = vec4(ht[r], 0.0);
    s_Hi[t + r] = vec4(hit[r], 0.0);
    s_Mid[t + r] = vec4(zt[r], 0.0);
  }
  s_size[s_found] = size_px;
  s_found = s_found + 1;
}
