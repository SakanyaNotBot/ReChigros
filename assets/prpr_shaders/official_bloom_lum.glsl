#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;

// PostEffects/luminance: the Bloom pass-0 prefilter. Chroma is halved, and the
// threshold scales the whole sample by clamp(lum-threshold,0,1)/lum.
uniform float bloomThreshold; // %0.308%

void main() {
  vec3 c = texture2D(screenTexture, uv).rgb;
  float lum = dot(c, vec3(0.212500006, 0.715399981, 0.0720999986));
  vec3 chroma = (c - vec3(lum)) * 0.5 + vec3(lum);
  float t = clamp(lum - bloomThreshold, 0.0, 1.0) / max(lum, 1.0e-5);
  gl_FragColor = vec4(chroma * t, 1.0);
}
