#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform vec2 screenSize;

// PostEffects/DimensionSlash verbatim port. Each slash builds a signed distance
// from _SlashPosition/_SlashNormal; the light is SlashColor * exp(-|d|*falloff) *
// strength * SlashLightStrength, and the sample uv is refracted by
// sign(d) * _SlashOffset (offset = (cos(ld+sd)/aspect, -sin(ld+sd)) * strength*0.01,
// normal = (sin(ld)*aspect, cos(ld)), degrees -> radians via pi/180).
uniform float slash1PosX; // %0.5%
uniform float slash1PosY; // %0.41%
uniform float slash1LineDir; // %15%
uniform float slash1SlashDir; // %100%
uniform float slash1Strength; // %0%
uniform float slash2PosX; // %0.5%
uniform float slash2PosY; // %0.41%
uniform float slash2LineDir; // %-15%
uniform float slash2SlashDir; // %0%
uniform float slash2Strength; // %0%
uniform float slash3PosX; // %0.5%
uniform float slash3PosY; // %0.41%
uniform float slash3LineDir; // %0%
uniform float slash3SlashDir; // %0%
uniform float slash3Strength; // %0%
uniform float slash4PosX; // %0.5%
uniform float slash4PosY; // %0.41%
uniform float slash4LineDir; // %0%
uniform float slash4SlashDir; // %0%
uniform float slash4Strength; // %0%
uniform float slash5PosX; // %0.5%
uniform float slash5PosY; // %0.41%
uniform float slash5LineDir; // %0%
uniform float slash5SlashDir; // %0%
uniform float slash5Strength; // %0%
uniform float slashCount;         // %5%
uniform float SlashLightStrength; // %30%
uniform float SlashLightFalloff;  // %35%
uniform vec3 slashLightColor;     // %0.759434,0.93157786,1%

const float DEG = 0.01745329238474369;

vec2 slashOffset(float ld, float sd, float strength, float asp) {
  float rad = (ld + sd) * DEG;
  return vec2(cos(rad) / asp, -sin(rad)) * (strength * 0.01);
}

vec2 slashNormal(float ld, float asp) {
  float rad = ld * DEG;
  return vec2(sin(rad) * asp, cos(rad));
}

void applySlash(float px, float py, float ld, float sd, float strength,
                float asp, inout vec2 suv, inout vec3 light) {
  vec2 n = slashNormal(ld, asp);
  float d = dot(suv - vec2(px, py), n);
  // Slash strengths arrive pre-scaled by 0.01 in the first component of a
  // four-component record. The fragment then
  // computes light = color * exp(-|d|*falloff) * (_SlashStrength.x * _SlashLightStrength),
  // i.e. strength*0.01*30 - NOT strength*30. Without the 0.01 every slash became a
  // ~280 px wide saturated white band (the "burst flash region/intensity" bug).
  light += slashLightColor * (exp(-abs(d) * SlashLightFalloff) * (strength * 0.01 * SlashLightStrength));
  // The same 0.01 is already applied to the refraction offset in slashOffset().
  suv += vec2(d < 0.0 ? -1.0 : 1.0) * slashOffset(ld, sd, strength, asp);
}

void main() {
  float asp = screenSize.x / max(screenSize.y, 1.0);
  vec2 suv = uv;
  vec3 light = vec3(0.0);
  if (4.0 < slashCount) applySlash(slash5PosX, slash5PosY, slash5LineDir, slash5SlashDir, slash5Strength, asp, suv, light);
  if (3.0 < slashCount) applySlash(slash4PosX, slash4PosY, slash4LineDir, slash4SlashDir, slash4Strength, asp, suv, light);
  if (2.0 < slashCount) applySlash(slash3PosX, slash3PosY, slash3LineDir, slash3SlashDir, slash3Strength, asp, suv, light);
  if (1.0 < slashCount) applySlash(slash2PosX, slash2PosY, slash2LineDir, slash2SlashDir, slash2Strength, asp, suv, light);
  if (0.0 < slashCount) applySlash(slash1PosX, slash1PosY, slash1LineDir, slash1SlashDir, slash1Strength, asp, suv, light);
  vec2 wuv = vec2(1.0) - abs(fract(abs(suv) * 0.5) * 2.0 - 1.0);
  vec3 base = texture2D(screenTexture, wuv).rgb;
  gl_FragColor = vec4(base + light, 1.0);
}
