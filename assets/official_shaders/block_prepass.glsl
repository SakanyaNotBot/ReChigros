#version 100
// Block pre-passes: compose (both layers), edge and glow dilation.
//   mode 0  compose -> (enabled, disabled) in R/G at screen/8, Point
//   mode 2  edge    -> effectRT.R at screen/4 (one dilate iteration)
//   mode 3  glow    -> ping-pong, y accumulates weighted rings outside compose
// Precision follows the shipped shaders: the displacement path is mediump.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 uv;

uniform sampler2D displaceMap;   // BlockNoise1
uniform sampler2D maskA;         // R active-normal | G active-subtract
                                 // B disabled/ready-normal | A unused
uniform sampler2D maskB;         // R disabled/ready-subtract | G ready-normal
                                 // B ready-subtract | A touch-hover
uniform sampler2D composeRT;     // edge/glow input
uniform sampler2D pingTex;       // glow input (x running max, y accumulation)

uniform int   passMode;
uniform float passWeight;
uniform float glowFirstPass;
uniform vec2  dilateTexel;       // 1 / effectRT size

uniform float time;              // t / 20
uniform vec2  displaceDirection;
uniform vec4  composeParams;     // _DisplaceMap_ST.xy, speed, strength

// Two scrolled BlockNoise1 samples combined as dir*(n1-0.5)+perp*(n2-0.5).
vec2 composeOffset(vec2 p) {
    mediump vec2 dir = normalize(displaceDirection);
    highp float s = time * composeParams.z;
    vec2 st = p * composeParams.xy;
    mediump float n1 = texture2D(displaceMap, st + dir * s).r;
    mediump float n2 = texture2D(displaceMap, st + vec2(-dir.y, dir.x) * s).r;
    return dir * (n1 - 0.5) + vec2(-dir.y, dir.x) * (n2 - 0.5);
}

// Unlit/SubtractBlockBlender pass 0 (SubtractBlockBlender.mat _ClampThresholdLow
// 0.09 / _ClampThresholdHigh 0.12, applied to the subtract camera RT by
// SubtractBlockPostProcessor.OnRenderImage): (v>=low?1:0) + (v>=high?-1:0).
// One sprite writes 0.1 -> +1; two overlapping sprites write 0.2 -> -1 (the hole
// cancels instead of being dropped).
float gate(float v) {
    if (v >= 0.12) return -1.0;
    if (v >= 0.09) return 1.0;
    return 0.0;
}

/** max of the 8 neighbours of p (the caller already holds the center texel). */
float max8(vec2 p, sampler2D t) {
    vec2 e = dilateTexel;
    float m = texture2D(t, p + vec2(e.x, 0.0)).r;
    m = max(m, texture2D(t, p + vec2(0.0, e.y)).r);
    m = max(m, texture2D(t, p + vec2(-e.x, 0.0)).r);
    m = max(m, texture2D(t, p + vec2(0.0, -e.y)).r);
    m = max(m, texture2D(t, p + vec2(-e.x, e.y)).r);
    m = max(m, texture2D(t, p + vec2(e.x, -e.y)).r);
    m = max(m, texture2D(t, p + vec2(e.x, e.y)).r);
    m = max(m, texture2D(t, p + vec2(-e.x, -e.y)).r);
    return m;
}

void main() {
    if (passMode == 0) {
        vec2 p = uv + composeOffset(uv) * composeParams.w;
        vec4 maP = texture2D(maskA, p);
        float mbP = texture2D(maskB, p).r;
        float enabled = abs(maP.r - gate(maP.g));
        // The disabled (pre-warning) silhouette is NOT displaced: ActiveBlock
        // samples the disabled pair at the plain screen uv, the effect chain
        // never touches it, and the warning has to show the true hit area.
        vec4 ma = texture2D(maskA, uv);
        float mb = texture2D(maskB, uv).r;
        float disabled = abs(ma.b - gate(mb));
        gl_FragColor = vec4(enabled, disabled, 0.0, 1.0);
        return;
    }
    if (passMode == 2) {
        float c = texture2D(composeRT, uv).r;
        float m = max(c, max8(uv, composeRT));
        gl_FragColor = vec4(clamp(m - c, 0.0, 1.0), 0.0, 0.0, 1.0);
        return;
    }
    vec2 cxy = texture2D(pingTex, uv).xy;
    float m = max(cxy.x, max8(uv, pingTex));
    float band = clamp(m - cxy.x, 0.0, 1.0);
    float prev = glowFirstPass > 0.5 ? 0.0 : cxy.y;
    float y = clamp(prev + band * (1.0 - texture2D(composeRT, uv).r) * passWeight, 0.0, 1.0);
    gl_FragColor = vec4(m, y, 0.0, 1.0);
}
