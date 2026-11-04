precision highp float;
varying vec2 uv;
uniform sampler2D blockMask;
uniform vec4 tint; // %0.1,0.9,0.3,1%
void main() {
  float word = floor(texture2D(blockMask, uv).r * 255.0 + 0.5);
  float visible = step(0.5, mod(word, 2.0));
  gl_FragColor = vec4(tint.rgb * visible, 1.0);
}
