#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform float time;

// Simple vignette pass including its scanline overlay.
uniform float vignetteDarkness; // %0.0%
uniform float vignetteRadius;   // %0.24%
uniform float vignetteSmoothness; // %0.55%
uniform float vignetteLineOpacity; // %0.0%

void main() {
  vec4 src = texture2D(screenTexture, uv);
  float d = length(uv - vec2(0.5)) - vignetteRadius;
  float v = clamp(d / max(vignetteSmoothness, 1.0e-5), 0.0, 1.0);
  v = v * v * (3.0 - 2.0 * v);
  float k = abs(1.0 - v * vignetteDarkness);
  float s = max((sin(uv.y * 850.0) * 0.5 - 0.25) * 4.0, 0.0);
  s = s * s * (3.0 - 2.0 * s);
  gl_FragColor = vec4(k * (src.rgb + s * vignetteLineOpacity), src.a);
}
