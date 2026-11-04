#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform float time;
uniform vec2 screenSize;

// PostEffects/LightBand verbatim port: _BandNormal = (-sin(dir)*aspect, cos(dir)),
// the band inverts colours inside it (mix toward 1-src) and drops saturation
// proportionally to the band profile m.
uniform float bandWidth;      // %0.0%
uniform float bandSmoothness; // %0.3%
uniform float bandBrightness; // %1.0%
uniform float bandDir;        // %0.0%

void main() {
  float asp = screenSize.x / max(screenSize.y, 1.0);
  float rad = bandDir * (3.14159274 / 180.0);
  vec2 n = vec2(-sin(rad) * asp, cos(rad));
  vec2 bp = vec2(0.5);
  float dist = abs(dot(uv - bp, n)) - bandWidth * 0.5;
  float x = clamp(dist / max(bandSmoothness, 1.0e-5), 0.0, 1.0);
  float m = 1.0 - x * x * (3.0 - 2.0 * x);
  float band = clamp(m * bandBrightness, 0.0, 1.0);
  vec4 src = texture2D(screenTexture, uv);
  vec3 c = src.rgb + band * ((vec3(1.0) - src.rgb) - src.rgb);
  float lum = dot(c, vec3(0.2125, 0.7154, 0.0721));
  c = (1.0 - 2.0 * m) * (c - lum) + lum;
  gl_FragColor = vec4(c, src.a);
}
