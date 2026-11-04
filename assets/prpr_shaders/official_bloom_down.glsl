#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;

// DualKawase down (PostEffects/Bloom pass 1): center x4 plus the four diagonal
// taps at 0.5*texel*(radius+1), divided by 8.
uniform vec2 texelSize;       // %0.001,0.001%
uniform float bloomRadius;    // %1.1%

void main() {
  vec2 o = texelSize * ((bloomRadius + 1.0) * 0.5);
  vec4 c = texture2D(screenTexture, uv) * 4.0;
  c += texture2D(screenTexture, uv + vec2(o.x, o.y));
  c += texture2D(screenTexture, uv + vec2(-o.x, -o.y));
  c += texture2D(screenTexture, uv + vec2(o.x, -o.y));
  c += texture2D(screenTexture, uv + vec2(-o.x, o.y));
  gl_FragColor = c * 0.125;
}
