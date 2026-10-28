precision highp float;
varying vec2 uv;
uniform sampler2D screenTexture;
uniform sampler2D blockMask;
uniform sampler2D maskedTexture;
uniform float gain; // %1%
void main() {
  float word = floor(texture2D(blockMask, uv).r * 255.0 + 0.5);
  float visible = step(0.5, mod(word, 2.0));
  vec3 scene = texture2D(screenTexture, uv).rgb;
  vec3 painted = texture2D(maskedTexture, uv).rgb * gain;
  gl_FragColor = vec4(mix(scene, painted + vec3(0.0, 0.15, 0.25), visible), 1.0);
}
