#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform float time;

// PostEffects/RGBShift: off = dir(uv-center) * pow(len(uv-center)*_Deform, _Radius);
// G/B sample at 0.33/0.66 of that offset, R stays put.
uniform float shiftRadius;   // _Radius %
uniform float screenDeform;  // %0.45%

void main() {
  // The pass is skipped (plain copy) when radius >= 6.5. One preset idles at 10
  // between its dips, so without this gate the pow() would still perturb the
  // frame by up to ~3e-4 uv at the corners.
  if (shiftRadius >= 6.5) {
    gl_FragColor = texture2D(screenTexture, uv);
    return;
  }
  vec2 d = uv - vec2(0.5);
  float dist = max(length(d), 1.0e-6);
  vec2 dir = d / dist;
  float amt = exp2(log2(max(dist * screenDeform, 1.0e-6)) * max(shiftRadius, 0.0));
  vec2 off = dir * amt;
  vec4 c;
  c.r = texture2D(screenTexture, uv).r;
  c.g = texture2D(screenTexture, uv - off * 0.33).g;
  c.b = texture2D(screenTexture, uv - off * 0.66).b;
  c.a = 1.0;
  gl_FragColor = c;
}
