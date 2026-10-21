#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform sampler2D noiseCol;
uniform float time;

// PostEffects/VignettePlus composite pass verbatim port: lift with the
// procedural noise texture (max with 0.4*noise), the sin(y*850) scanline, then
// the radial darkening (starts at _Radius, fades over _Smooth).
uniform float vignetteRadius;          // %0.5%
uniform float vignetteDarkness;        // %0.0%
uniform float vignetteSmoothness;      // %0.3%
uniform float vignetteNoiseBrightness; // %0.0%
uniform float vignetteLineOpacity;     // %0.0%

void main() {
  vec4 src = texture2D(screenTexture, uv);
  vec3 c = src.rgb;
  if (vignetteNoiseBrightness > 0.0) {
    c = max(c, texture2D(noiseCol, uv).rgb * 0.4);
  }
  float s = max((sin(uv.y * 850.0) * 0.5 - 0.25) * 4.0, 0.0);
  s = s * s * (3.0 - 2.0 * s);
  c += s * vignetteLineOpacity;
  float d = length(uv - vec2(0.5)) - vignetteRadius;
  float v = clamp(d / max(vignetteSmoothness, 1.0e-5), 0.0, 1.0);
  v = v * v * (3.0 - 2.0 * v);
  gl_FragColor = vec4(abs(1.0 - v * vignetteDarkness) * c, src.a);
}
