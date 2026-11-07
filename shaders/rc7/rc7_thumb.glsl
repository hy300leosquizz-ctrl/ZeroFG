// The fast camera, past the dense flow's reach: a small picture's blocks over a wide reach give coarse votes, and the model they agree on is refined around what it predicts on finer pictures.
// Only when the surfaces of the flow leave half the picture unexplained: otherwise every dispatch here is empty.
layout(std430, binding = 26) buffer ThumbBuf {
  uvec4 t_ind_block;    // dispatch arguments, zero when this does not run
  uvec4 t_ind_refine;
  uvec4 t_ind_guide;
  int t_on;
  int t_step;           // pixels per point of the small picture
  int t_tw;
  int t_th;
  int t_bw;
  int t_bh;
  int t_reach;
  int t_accept;
  int t_trusted;
  int t_unexplained;
  int t_count;
  int t_gw;             // the current level of the guided search
  int t_gh;
  int t_gstep;
  int t_pad0;
  int t_pad1;
  int t_pic[4];
  vec4 t_cand[12];      // [candidate][row]: where each candidate starts
  vec4 t_placed[12];    // ... and placed by two rounds, for the pictures to judge
  vec4 t_cell[4480];    // the vector, 1 in z while it counts, in the current level's cells
  int t_best[4480];     // the best match of each block: the error in the high bits, the shift in the low 13
  int t_sum[4480];
  int t_tried[4480];
  float t_score[4480];
  float t_luma[];       // [side][point] of the small picture
};

const int BLOCK = 6;
const int COARSE = 2;
const float TLUMA_R = 0.2126, TLUMA_G = 0.7152, TLUMA_B = 0.0722;
float lumaOf(vec3 c) { return TLUMA_R * c.r + TLUMA_G * c.g + TLUMA_B * c.b; }
