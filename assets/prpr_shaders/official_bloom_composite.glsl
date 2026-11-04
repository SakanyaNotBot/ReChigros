#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;

// PostEffects/Bloom composite (pass 3): c1 = mix(blur, blur^2, clamp(intensity-1,0,1)),
// out = 1 - (1 - c1*_BloomColor) * (1 - src). _BloomColor = preset color * brightness.
uniform sampler2D blurTex;
uniform float bloomIntensity; // %1.85%
uniform vec3 bloomColor;      // %1,1,1%

void main() {
  vec4 src = texture2D(screenTexture, uv);
  vec3 blur = texture2D(blurTex, uv).rgb;
  float t = clamp(bloomIntensity - 1.0, 0.0, 1.0);
  vec3 c1 = blur + t * (blur * blur - blur);
  vec3 o = vec3(1.0) - (vec3(1.0) - c1 * bloomColor) * (vec3(1.0) - src.rgb);
  gl_FragColor = vec4(clamp(o, 0.0, 1.0), src.a);
}
