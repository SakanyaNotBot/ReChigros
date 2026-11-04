#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform float time;

// PostEffects/ESCContol (Exposure/Saturate/Contrast).
uniform float brightness; // %1.0%
uniform float saturation; // %1.0%
uniform float contrast;   // %1.0%
uniform vec3 averageColor;// %0.5,0.5,0.5%

void main() {
  vec4 src = texture2D(screenTexture, uv);
  float lum = dot(src.rgb, vec3(0.2125, 0.7154, 0.0721));
  vec3 c = saturation * (src.rgb - lum) + lum;
  c = contrast * (c - averageColor) + averageColor;
  gl_FragColor = vec4(c * brightness, src.a);
}
