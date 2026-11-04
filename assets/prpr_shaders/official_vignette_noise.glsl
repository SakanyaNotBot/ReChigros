#version 100
precision highp float;
varying highp vec2 uv;
uniform float time;

// PostEffects/VignettePlus noise pass (pass 1) verbatim port: procedural HSV
// hash noise driven by _SinTime and gl_FragCoord, rendered into a
// noiseSampleRate-scaled RT each frame while noiseBrightness > 0.
uniform float noiseBrightness;  // %0.0%
uniform float noiseSaturation;  // %0.0%

void main() {
  vec4 sinT = vec4(sin(time * 0.125), sin(time * 0.25), sin(time * 0.5), sin(time));
  vec4 fc = vec4(gl_FragCoord.xyz, 1.0);
  vec4 u0 = fc.xyyx * sinT.xyyx;
  vec4 u1 = fract(u0 * 0.1031 + vec4(6.09623432, 8.60783958, 8.60783958, 6.09623432));
  vec4 u0b = fract(u0.wzwz * 0.1031 + vec4(1.52148008, 7.41844368, 4.41411209, 2.25424623));
  u1 += vec4(0.322998047, 0.322998047, 33.6529999, 33.6529999);
  float d1 = u1.x * u1.z + u1.y * u1.w + u1.x * u1.w;
  u1.xy = vec2(d1) + u1.xy;
  float s1 = u1.y + u1.x;
  u1.x = u1.x * s1;
  vec4 u2 = u0b.zwwz + vec4(0.374389648, 0.374389648, 33.7043915, 33.7043915);
  vec4 u0c = u0b.xyyx + vec4(0.124023438, 0.124023438, 33.4540253, 33.4540253);
  float d2 = u2.x * u2.z + u2.y * u2.w + u2.x * u2.w;
  vec2 u6 = vec2(d2) + u2.xy;
  float s2 = u6.y + u6.x;
  u1.y = u6.x * s2;
  u1.xy = fract(u1.xy);
  float bA = (u1.y >= u1.x) ? 1.0 : 0.0;
  vec4 m2;
  m2.y = bA * (u1.x - u1.y) + u1.y;
  m2.x = bA * (u1.y - u1.x) + u1.x;
  m2.zw = vec2(bA) * vec2(1.0, -1.0) + vec2(-1.0, 0.666666687);
  float t0 = dot(u0c.xyx, u0c.zww);
  u0c.xy = vec2(t0) + u0c.xy;
  float s3 = u0c.y + u0c.x;
  float hx = fract(u0c.x * s3);
  float bB = (hx >= m2.x) ? 1.0 : 0.0;
  vec3 m3;
  m3.x = bB * (hx - m2.x) + m2.x;
  m3.y = m2.y;
  m3.z = bB * (m2.z - m2.w) + m2.w;
  float m18 = bB * (m2.x - hx) + hx;
  float mMin = min(m3.y, m18);
  float mRange = m18 - m3.y;
  float mDelta = m3.x - mMin;
  float h6 = mRange / (mDelta * 6.0 + 1.0e-10) + m3.z;
  vec3 hue = fract(abs(vec3(h6)) + vec3(1.0, 0.666666687, 0.333333343)) * 6.0 - 3.0;
  hue = clamp(abs(hue) - 1.0, 0.0, 1.0) - 1.0;
  float base = m3.x * noiseBrightness;
  float sat = (mDelta / (m3.x + 1.0e-10)) * noiseSaturation;
  vec3 outc = base * (vec3(1.0) + sat * hue);
  gl_FragColor = vec4(clamp(outc, 0.0, 1.0), 1.0);
}
