#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D screenTexture;
uniform float time;

// PostEffects/LensDistortion verbatim port. The shipped SetMaterialData scales
// both _Size and _Convergence by 0.01 and passes the settings' manualAspect as
// _Aspect (useScreenAspect is false for every Chapter 9 preset).
uniform float convergence;   // _Convergence %0%
uniform float lensSize;      // _Size %50%
uniform float edgeSmoothness;// %0%
uniform float lensAspect;    // %1.6%

void main() {
  vec2 c = vec2(0.5);
  vec2 cd = vec2(max(1.0 - c.x, c.x), max(1.0 - c.y, c.y));
  float maxR = length(vec2(cd.x * lensAspect, cd.y)) * max(lensSize * 0.01, 1.0e-5);
  // Official (PostEffects/LensDistortion): _Aspect only widens the falloff radius;
  // the warp displacement itself uses the raw (uv - center), so convergence=0 is identity.
  vec2 d = uv - c;
  float r = length(vec2(d.x * lensAspect, d.y)) / max(maxR, 1.0e-5);
  float f = 1.0 - r * r * (convergence * 0.01);
  vec2 warped = d * f + c;
  vec4 col = texture2D(screenTexture, warped);
  vec2 e = min(vec2(1.0) - warped, warped);
  float em = clamp(min(e.x, e.y) / max(edgeSmoothness, 1.0e-5), 0.0, 1.0);
  em = em * em * (3.0 - 2.0 * em);
  gl_FragColor = col * em;
}
