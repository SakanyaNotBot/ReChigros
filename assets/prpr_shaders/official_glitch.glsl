#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform sampler2D glitchMap;
uniform float time;

// PostEffects/Glitch verbatim port. The official shader derives the per-layer
// direction/jitter hashes from _Time in the vertex stage of the fullscreen blit,
// so every pixel of a frame shares one hash triple (offsets vary per pixel only
// through the GlitchTex RGB gate); each channel samples its own accumulated uv.
uniform float glitchRange;   // %0.0%
uniform float rgbChannelOffset; // %0.0%
uniform float layer1Speed;   // %0.2%
uniform float layer2Speed;   // %0.1%
uniform float layer3Speed;   // %0.1%
uniform vec2 layer1Strength; // %1,2%
uniform vec2 layer2Strength; // %0.5,0.5%
uniform vec2 layer3Strength; // %1,2%

vec2 hashPair(float a, float b) {
  float m0 = (a + 33.5540237) * (a + 0.224023432);
  float m1 = (b + 33.80439) * (b + 0.474389642);
  return fract(vec2(m0 * m0, m1 * m1));
}

vec2 scaledDir(vec2 h, float cosT, vec2 strength) {
  vec2 d = h * cosT;
  return d * inversesqrt(max(dot(d, d), 1.0e-8)) * strength;
}

void main() {
  vec3 t3 = vec3(time, 2.0 * time, 3.0 * time);
  float cosT = cos(time);
  vec3 speedT = t3 * vec3(layer1Speed, layer2Speed, layer3Speed);
  vec4 e12 = fract(speedT.xxyy * 0.1031 + vec4(0.546, 0.153, 0.546, 0.153));
  vec2 dir1 = scaledDir(hashPair(e12.x, e12.y), cosT, layer1Strength);
  vec2 dir2 = scaledDir(hashPair(e12.z, e12.w), cosT, layer2Strength);
  vec2 e3 = fract(speedT.zz * 0.1031 + vec2(0.546, 0.153));
  vec2 dir3 = scaledDir(hashPair(e3.x, e3.y), cosT, layer3Strength);
  vec3 pj = fract(t3 * 0.1031 + vec3(0.1919, 0.81, 0.1145));
  float r1 = (pj.x + 33.4939346) * (pj.x + 0.16393432);
  float r2 = (pj.y + 34.2789078) * (pj.y + 0.948907495);
  float q = (pj.z + 34.0607834) * (pj.z + 0.730783105);
  vec3 jit = fract(vec3(r1 * r1, r2 * r2, q * q));

  vec3 g = (texture2D(glitchMap, uv).rgb - 0.5) * glitchRange;
  vec2 p1 = g.xx * dir1 * jit.xx + uv;
  vec2 p2 = g.yy * dir2 * jit.yy + p1;
  vec2 p3 = g.zz * dir3 * jit.zz + p2;
  vec4 c;
  // The shipped presets keep the per-channel offset disabled, which selects the
  // single-sample variant: the screen is sampled once at p3 after all three
  // displacements. The split-channel variant only runs when that flag is set.
  c = texture2D(screenTexture, p3);
  c.a = 1.0;
  gl_FragColor = c;
}
