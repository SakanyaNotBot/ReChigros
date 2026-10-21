#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform float time;
uniform sampler2D wireNoise;  // 11 frames stacked vertically (_WireNoiseArray)
uniform sampler2D displaceMap;
uniform sampler2D glitchDogMap;  // GlitchDogSettings.glitchMap (GlitchMap_0.png, R-only, Point+Repeat)
uniform vec2 screenSize;

// PostEffects/GlitchDog verbatim port. Polar space is (v1.yx - 0.5) where
// v1 = ((uv - 0.5) * vec2(aspect, 1)) / _Scale; the displace map perturbs the
// polar coordinates BEFORE the masks are evaluated, which is what turns the
// edge mask into needles instead of an ellipse. Final blend:
// src + gate*(abs(src - col) - src), col = mix(mask, mask*tint, colorBlend).
uniform float glitchDogScale;        // %1.8%
uniform float glitchMapBrightness;   // %0.0%
uniform float glitchMapContrast;     // %3.5%
uniform vec2 glitchMapOffset;        // %0,0%
uniform vec2 glitchMapScale;         // %5.6,1%
uniform float noiseFrameCount;       // %11%
uniform float noiseMoveSpeed;        // %2%
uniform float noiseEvolveSpeed;      // %2%
uniform vec2 noiseScale;             // %0.3,0.36%
uniform vec2 noiseOffset;            // %0,0%
uniform float seamBlend;             // %0.02%
uniform float displaceStrength;      // %0.2%
uniform vec2 displaceMapOffset;      // %0.11,0%
uniform vec2 displaceMapScale;       // %1,1%
uniform float displaceSpeed;         // %1%
uniform float centerMaskRadius;      // %0.094%
uniform float centerMaskSmoothness;  // %0.07%
uniform float edgeMaskRadius;        // %0.12%
uniform float edgeMaskSmoothness;    // %0.41%
uniform vec3 centerColor;            // %0,0.2896262,0.6037736%
uniform vec3 edgeColor;              // %0.06194376,0.08723756,0.1509434%
uniform float colorBlend;            // %0.776%

float tri(float v) { return 1.0 - abs(fract(abs(v) * 0.5) * 2.0 - 1.0); }
float smooth1(float x) { x = clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }

// The shipped octant-polynomial atan2 approximation.
float polyAtan(vec2 d) {
  float mx = max(abs(d.y), abs(d.x));
  float mn = min(abs(d.y), abs(d.x));
  float n = mn / max(mx, 1.0e-10);
  float n2 = n * n;
  float p = n2 * 0.0208350997 + -0.0851330012;
  p = n2 * p + 0.180141002;
  p = n2 * p + -0.330299497;
  p = n2 * p + 0.999866009;
  float a = p * n;
  a = a * -2.0 + 1.57079637;
  a = (abs(d.y) < abs(d.x)) ? a : 0.0;
  float r = n * p + a;
  r += (d.y < -d.y) ? -3.14159274 : 0.0;
  bool flip = (min(d.y, d.x) < -min(d.y, d.x)) && (max(d.y, d.x) >= -max(d.y, d.x));
  return flip ? -r : r;
}

// Textures are uploaded V-flipped so the original v-origin is reproduced. The slice
// stack keeps its frame order and v = (layer + localV)/frames samples it like an
// array texture.
float sampleWireNoise(vec2 n, float layer) {
  float inset = 1.0 / 1024.0;
  vec2 a = n * (1.0 - 2.0 * inset) + inset;
  return texture2D(wireNoise, vec2(a.x, (layer + a.y) / noiseFrameCount)).x;
}

void main() {
  float aspect = screenSize.x / max(screenSize.y, 1.0);
  float sc = max(glitchDogScale, 1.0e-5);
  vec2 q = vec2((uv.y - 0.5), (uv.x - 0.5) * aspect) / sc;
  float radius = length(q);
  float ang = polyAtan(q);
  vec2 polar = vec2(radius, ang);

  float t20 = time * 0.05;
  vec2 duv = vec2(t20) * displaceSpeed + displaceMapOffset + polar;
  duv = (duv - 0.5) * displaceMapScale + 0.5;
  duv = vec2(tri(duv.x), tri(duv.y));
  float disp = texture2D(displaceMap, duv).x - 0.5;
  float dOff = disp * displaceStrength;
  vec2 dpolar = polar + vec2(dOff);

  // Noise uv uses the original polar + scroll; the displacement shifts the sample.
  vec2 nuv = polar + vec2(t20) * noiseMoveSpeed + noiseOffset;
  nuv = (nuv - 0.5) * noiseScale + vec2(dOff) + 0.5;
  nuv = vec2(tri(nuv.x), tri(nuv.y));
  float frame = fract(time * noiseEvolveSpeed / noiseFrameCount) * noiseFrameCount;
  float f0 = floor(frame);
  float fy = fract(frame);
  float f1 = f0 + 1.0;
  if (f1 >= noiseFrameCount) f1 = f1 - noiseFrameCount;
  float n0 = sampleWireNoise(nuv, f0);
  float n1 = sampleWireNoise(nuv, f1);
  float noise = mix(n0, n1, fy);
  // Seam gate: t = clamp((PI - |angle|) / (_SeamBlend * PI), 0, 1), then
  // smoothstep(t). It is symmetric around angle 0 (the seam), not a signed gate.
  float seam = clamp((3.14159274 - abs(ang)) / max(seamBlend * 3.14159274, 1.0e-8), 0.0, 1.0);
  noise *= smooth1(seam);

  // Masks on the displaced radius. The centre mask uses
  // (|r'| - centerMaskRadius) / centerMaskSmoothness,
  // edgeMask uses (|r'|-edgeMaskRadius)/edgeMaskSmoothness, and the final
  // gate is `noise * centerMask + edgeMask`. The radii and smoothness belong to different masks and must not be exchanged.
  float dRadius = abs(dpolar.x);
  float cMask = smooth1((dRadius - centerMaskRadius) / max(centerMaskSmoothness, 1.0e-5));
  float eMask = smooth1((dRadius - edgeMaskRadius) / max(edgeMaskSmoothness, 1.0e-5));
  float mask = clamp(noise * cMask + eMask, 0.0, 1.0);

  vec3 tint = centerColor + (edgeColor - centerColor) * colorBlend;
  float cb = clamp(colorBlend, 0.0, 1.0);
  vec3 col = mask + cb * (mask * tint - mask);

  vec2 guv = (uv + glitchMapOffset - 0.5) * glitchMapScale + 0.5;
  float gm = texture2D(glitchDogMap, guv).x;
  // Official clamps once, after brightness is folded in.
  gm = clamp(((gm - 0.5) * glitchMapContrast + 0.5) * glitchMapBrightness, 0.0, 1.0);

  vec4 src = texture2D(screenTexture, uv);
  vec3 outc = src.rgb + gm * (abs(src.rgb - col) - src.rgb);
  gl_FragColor = vec4(outc, src.a);
}
