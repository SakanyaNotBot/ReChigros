#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;

// DualKawase up (PostEffects/Bloom pass 2), verbatim: four axial taps at
// radius+1 texels and four diagonal taps at half that, diagonals weighted 2,
// normalized by 1/12 (0.083329998 in the shipped shader).
uniform vec2 texelSize;       // %0.001,0.001%
uniform float bloomRadius;    // %1.1%

void main() {
  float k = bloomRadius + 1.0;
  vec2 t = texelSize * k;
  vec2 h = texelSize * (k * 0.5);
  vec4 c = texture2D(screenTexture, uv + vec2(-t.x, 0.0));
  c += texture2D(screenTexture, uv + vec2(-h.x, h.y)) * 2.0;
  c += texture2D(screenTexture, uv + vec2(0.0, t.y));
  c += texture2D(screenTexture, uv + vec2(h.x, h.y)) * 2.0;
  c += texture2D(screenTexture, uv + vec2(t.x, 0.0));
  c += texture2D(screenTexture, uv + vec2(h.x, -h.y)) * 2.0;
  c += texture2D(screenTexture, uv + vec2(0.0, -t.y));
  c += texture2D(screenTexture, uv + vec2(-h.x, -h.y)) * 2.0;
  gl_FragColor = c * 0.083329998;
}
