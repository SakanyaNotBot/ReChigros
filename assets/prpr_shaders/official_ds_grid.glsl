#version 100
precision highp float;
varying highp vec2 uv;
uniform sampler2D gridTex;
uniform float time;            // chart clock (_Time.y)
uniform vec2 texST;            // _MainTex_ST.xy tiling
uniform vec3 gridColor;
uniform float gridBrightness;  // %0.57%
uniform vec3 bloomColor;
uniform float bloomBrightness; // %0.59%
uniform float scaleSpeed;      // _ScaleSpeed (frozen to 0 at the level freeze)
uniform vec4 vertexColor;      // (1,1,1, gridOpacity)

void main() {
  float ft = fract(time * scaleSpeed);
  float e = exp2(-ft);
  vec2 uv1 = ((uv - vec2(0.5)) * e + vec2(0.5)) * texST;
  vec2 uv2 = ((uv - vec2(0.5)) * (2.0 * e) + vec2(0.5)) * texST;
  vec2 t1 = texture2D(gridTex, uv1).xy;
  vec2 t2 = texture2D(gridTex, uv2).xy;
  float bb2 = bloomBrightness * bloomBrightness;
  vec3 c1 = t1.y * bloomColor * bb2 * bloomBrightness
          + t1.x * gridColor * gridBrightness;
  vec3 c2 = t2.y * bloomColor * bb2 * bloomBrightness
          + t2.x * gridColor * gridBrightness;
  float k = clamp(bloomBrightness * 10.0, 0.0, 1.0);
  float a1 = clamp(1.0 - (1.0 - t1.x * gridBrightness) * (1.0 - t1.y * k), 0.0, 1.0);
  float a2 = clamp(1.0 - (1.0 - t2.x * gridBrightness) * (1.0 - t2.y * k), 0.0, 1.0);
  vec3 rgb = a1 * c1 + ft * (a2 * c2 - a1 * c1);
  float a = a1 + ft * (a2 - a1);
  gl_FragColor = vec4(rgb, a) * vertexColor;
}
