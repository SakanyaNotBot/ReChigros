#version 100
// Full-screen block composite: zone fill, edge, glow, ready shine, disabled
// fill and touch effect over the pre-pass outputs of block_prepass.glsl.
// Zone pixels are REPLACED by the fill (a displaced, hue-shifted, spark-overlaid
// resample of the scene); everywhere else the scene passes through untouched.
//
// Samplers (8 max - a real-device GLES limit):
//   screenTexture  scene colour copy
//   displaceMap    BlockNoise1    sparkMap PointNoise    noiseMap FD_Noise_00000
//   composeRT      (enabled, disabled) displaced composes at screen/8 Point
//   effectRT       x = edge ring, y = glow accumulation (screen/4 Bilinear)
//   maskA  R active-normal | G active-subtract
//          B disabled/ready-normal (DisabledBlockShow fade accumulated)
//   maskB  R disabled/ready-subtract | G ready-normal
//          B ready-subtract fade | A touch-hover camera (screen/8 Point)
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 uv;

uniform sampler2D screenTexture;
uniform sampler2D displaceMap;
uniform sampler2D sparkMap;
uniform sampler2D noiseMap;
uniform sampler2D composeRT;
uniform sampler2D effectRT;
uniform sampler2D maskA;
uniform sampler2D maskB;

uniform float time;                // t / 20
uniform float timeY;               // t
uniform vec2 screenSize;
// Actual stage rect in screen pixels, y-down (x0, y0, x1, y1). The zone
// effect is gated to this rect instead of a hard-coded 16:9 window, so the
// noise field can cover the whole stage on non-16:9 stages (and shrinks with
// the reveal window when the caller passes the scaled rect).
uniform vec4 stageRect;
uniform vec2 displaceDirection;    // (1,1) normalised

uniform vec4 matParams[8];
uniform vec4 matColors[7];
// [0] compose ST.xy, speed, strength       (2.13, 1.02, 2.59, 0.1)
// [1] active ST.xy, speed, strength        (0.8, 0.3, 1.5, 0.15)
// [2] spark ST.xy, disabled ST.xy          (3, 1.2, 0.5, 0.2)
// [3] disabled speed, spark displace, touch ST.xy (0.3, 2.29, 0.55, 0.3)
// [4] touch speed, strength, noise ST.xy   (2.9, 0.08, 1.5, 1.46)
uniform vec4 matExtra[5];

#define MAX_TOUCH 10
uniform int touchCount;
uniform vec2 touchPos[MAX_TOUCH];
/** (low + (1-low)*(sin(speed*t)*0.5+0.5)) * brightness, real time. */
uniform float touchShineValue;

vec2 perp(vec2 d) { return vec2(-d.y, d.x); }

/** Centres of ps x ps screen-pixel cells. */
vec2 snapPx(vec2 p, float ps) {
    vec2 s = screenSize / ps;
    return (floor(p * s) + 0.5) / s;
}

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1.0e-10)), d / (q.x + 1.0e-10), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec2 hash2Scalar(vec2 t) {
    vec4 p = fract(vec4(t.x * 0.103100002, 0.381470025, t.y * 0.103100002, 0.93821007));
    vec3 s = p.yxx + vec3(33.3300018);
    float d = dot(p.xyx, s);
    vec2 e = vec2(d) + p.xy;
    float h1 = fract(d * (e.x + e.y));
    s = p.wzz + vec3(33.3300018);
    d = dot(p.zwz, s);
    e = vec2(d) + p.zw;
    float h2 = fract(d * (e.x + e.y));
    return vec2(h1, h2) * 2.0 - 1.0;
}

float gridHash1(vec2 c) {
    vec2 p = fract(c * 0.103100002);
    vec3 s = p.yxx + vec3(33.3300018);
    float d = dot(p.xyx, s);
    vec2 e = vec2(d) + p;
    return fract(d * (e.x + e.y));
}

float smoothstep01(float t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/**
 * One step of the smooth-minimum union over the held touches. Unrolled by the
 * caller with literal uniform-array indices: ES 1.00 fragment shaders are only
 * guaranteed to accept constant indices on uniform arrays.
 */
float sdfMin(float dmin, vec2 tpos, vec2 touch, float aspect, float smoothness) {
    float d = length(tpos - vec2(touch.x * aspect, touch.y));
    float w = max(-abs(d - dmin) + smoothness, 0.0) / smoothness;
    d = min(d, dmin);
    return d - w * w * smoothness * 0.25;
}

void main() {
    float edgeOpacity    = matParams[0].x;
    float fillStrength   = matParams[0].y;
    float fillOpacity    = matParams[0].z;
    float glowIntensity  = matParams[0].w;
    float sparkOpacity   = matParams[1].x;
    float sparkHueShift  = matParams[1].y;
    float sparkDisplace  = matParams[1].z;
    float displaceBlend  = matParams[1].w;
    float bgPixelScale   = matParams[2].z;
    float shineSpeed     = matParams[2].w;
    float shineBrightness = matParams[3].x;
    float touchRadius    = matParams[3].z;
    float touchSmooth    = matParams[3].w;
    float touchFalloff   = matParams[4].x;
    float noiseEvoSpeed  = matParams[4].y;
    float noiseDirChange = matParams[4].z;
    float noiseDispStr   = matParams[4].w;
    float noiseRadius    = matParams[5].x;
    float noiseSmooth    = matParams[5].y;
    float sdfCellSize    = matParams[5].z;
    float sdfSmooth      = matParams[5].w;
    float sdfFalloff     = matParams[6].x;
    float sdfMoveSpeed   = matParams[6].y;

    vec2 activeST   = matExtra[1].xy;
    float activeSpeed = matExtra[1].z;
    float activeStr = matExtra[1].w;
    vec2 sparkST    = matExtra[2].xy;
    vec2 disabledST = matExtra[2].zw;
    float disabledSpeed = matExtra[3].x;
    float disabledSparkDisp = matExtra[3].y;
    float touchSpeed = matExtra[4].x;
    float touchStr  = matExtra[4].y;

    vec3 edgeColor  = matColors[0].rgb;
    vec3 fillColor  = matColors[1].rgb;
    vec3 glowColor  = matColors[2].rgb;
    vec3 sparkTint  = matColors[3].rgb;
    vec3 disabledFill = matColors[4].rgb;
    float disabledFillOp = matColors[4].w;
    vec3 touchGlow  = matColors[5].rgb;
    vec3 shineColor = matColors[6].rgb;

    vec3 scene = texture2D(screenTexture, uv).rgb;

    // displaceDirection is a uniform, so normalise it once instead of once per branch
    // (the active body, the touch hover and the pre-warning fill all need it).
    vec2 displaceDir = normalize(displaceDirection);
    vec2 displacePerp = perp(displaceDir);

    // Zone effect only inside the actual stage rect; elsewhere the scene
    // passes through. uv.y is GL bottom-up, so flip to y-down pixels first.
    vec2 stagePx = vec2(uv.x, 1.0 - uv.y) * screenSize;
    if (stagePx.x < stageRect.x || stagePx.x > stageRect.z
            || stagePx.y < stageRect.y || stagePx.y > stageRect.w) {
        gl_FragColor = vec4(scene, 1.0);
        return;
    }

    mediump vec2 composeRG = texture2D(composeRT, uv).rg;
    mediump float compose = composeRG.r;
    mediump float disabled = composeRG.g;

    // Unlit/DisabledBlock is its own full-screen pass in the official renderer, drawn over the
    // scene colour *before* Unlit/ActiveBlock composites the warped zone onto the result
    // (BlockRender.RefreshSceneColorCommands orders the two that way). Its runtime blend state is
    // src One, dst One (ADDITIVE - dumped from the shipped bundle), so it only brightens the
    // scene; it never dims it.
    //
    // It reads the pre-warning coverage out of its own compose RT (Unlit/BlockCompose pass 1:
    // abs(subtract.x * subtract.y - normal)) and writes
    //     RGB = coverage * (_FillColor * _FillOpacity + spark * _SparkMapOpacity * _SparkTint)
    //     A   = vs_COLOR0.w
    vec3 zoneUnder = scene;
    if (disabled > 9.99999975e-05) {
        highp float ds = time * disabledSpeed;
        vec2 dst = uv * disabledST;
        mediump float dn1 = texture2D(displaceMap, dst + displaceDir * ds).r;
        mediump float dn2 = texture2D(displaceMap, dst + displacePerp * ds).r;
        mediump float davg = (dn1 + dn2) * 0.5;
        mediump vec2 doff = displaceDir * (dn1 - 0.5) + displacePerp * (dn2 - 0.5);
        mediump float dspark = texture2D(sparkMap, uv * sparkST + doff * disabledSparkDisp).r;
        vec3 dRgb = disabled
                * (disabledFill * disabledFillOp
                   + davg * dspark * vec3(0.3113208, 0.0778302, 0.0778302) * 3.5);
        zoneUnder = zoneUnder + dRgb;
    }

    vec4 mB = texture2D(maskB, uv);
    // Ready shine gate: |readySubtract.g - readyNormal|, readyCompose = |sub.x*sub.g - n|.
    float rdyX = (mB.b > 0.0) ? 0.1 : 0.0;
    mediump float ready18 = abs(rdyX * mB.b - mB.g) * abs(mB.b - mB.g);

    vec2 eRes = screenSize * 0.25;
    vec2 eUv = (floor(uv * eRes) + 0.5) / eRes;
    mediump float edge = texture2D(effectRT, eUv).r;
    mediump float glowAcc = texture2D(effectRT, uv).g;

    // Hover camera output is point-filtered at screen/8.
    vec2 hRes = screenSize / 8.0;
    vec2 hUv = (floor(uv * hRes) + 0.5) / hRes;
    mediump float hover = texture2D(maskB, hUv).a;

    float u34 = edge + compose;
    float u50 = glowAcc + u34;

    vec3 body = vec3(0.0);
    float alphaOut = 0.0;
    bool bodyOn = 9.99999975e-05 < u50;
    if (bodyOn) {
        float edgeTerm = edge * edgeOpacity;
        float glowTerm = glowAcc * (1.0 - clamp(u34, 0.0, 1.0)) * glowIntensity;

        highp float s = time * activeSpeed;
        vec2 stA = uv * activeST;
        mediump float n1 = texture2D(displaceMap, snapPx(stA + displaceDir * s, bgPixelScale)).r;
        mediump float n2 = texture2D(displaceMap, snapPx(stA + displacePerp * s, bgPixelScale)).r;
        mediump float avg = (n1 + n2) * 0.5;
        mediump vec2 off = displaceDir * (n1 - 0.5) + displacePerp * (n2 - 0.5);

        // Scene snapshot: the shipped Unlit/ActiveBlock samples _SceneColor, which is a
        // screen/6 ARGB32 POINT render texture (BlockRender.Start / RefreshSceneColorCommands
        // copy the camera target into it). Sampling our full-res copy at the centre of the
        // /6 cell reproduces that point-sampled /6 texel exactly, so the spark overlay's
        // hue-shifted scene tint stays as blocky as the shipped one.
        vec2 sceneUv = snapPx(uv, bgPixelScale) + off * activeStr;
        vec3 refr = texture2D(screenTexture, snapPx(sceneUv, bgPixelScale)).rgb;
        mediump float spark = texture2D(sparkMap, uv * sparkST + off * sparkDisplace).r;

        float touchShine = 0.0;
        if (0 < touchCount) {
            vec2 tpos = (uv + off * activeStr) * screenSize / screenSize.y;
            float aspect = screenSize.x / screenSize.y;
            float dmin = sdfMin(1.0, tpos, touchPos[0], aspect, touchSmooth);
            if (1 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[1], aspect, touchSmooth);
            if (2 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[2], aspect, touchSmooth);
            if (3 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[3], aspect, touchSmooth);
            if (4 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[4], aspect, touchSmooth);
            if (5 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[5], aspect, touchSmooth);
            if (6 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[6], aspect, touchSmooth);
            if (7 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[7], aspect, touchSmooth);
            if (8 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[8], aspect, touchSmooth);
            if (9 < touchCount) dmin = sdfMin(dmin, tpos, touchPos[9], aspect, touchSmooth);
            float t = smoothstep01((dmin - touchRadius) / (-touchRadius));
            touchShine = pow(max(t, 1.0e-6), touchFalloff);
        }

        vec3 blend1 = avg * spark * sparkTint * sparkOpacity;
        vec3 hsv = rgb2hsv(refr) + blend1 * sparkHueShift;
        vec3 base = hsv2rgb(hsv);
        vec3 ov = mix(2.0 * base * blend1,
                      1.0 - 2.0 * (1.0 - base) * (1.0 - blend1),
                      step(vec3(0.5), base));
        vec3 fill = mix(fillColor - avg * displaceBlend, ov, fillStrength);
        body = fill * compose + edgeColor * edgeTerm + glowColor * glowTerm;
        body *= touchShine * touchShineValue + 1.0;
        alphaOut = clamp(glowTerm + compose * fillOpacity + edgeTerm, 0.0, 1.0);
    }

    vec3 shine = vec3(0.0);
    if (9.99999975e-05 < ready18) {
        shine = ready18 * ((sin(timeY * shineSpeed) * 0.5 + 1.0) * shineBrightness * shineColor);
    }

    vec3 touchCol = vec3(0.0);
    if (9.99999975e-05 < hover) {
        highp float ts = time * touchSpeed;
        vec2 tst = uv * vec2(0.55, 0.3);
        mediump float tn1 = texture2D(displaceMap, snapPx(tst + displaceDir * ts, 8.0)).r;
        mediump float tn2 = texture2D(displaceMap, snapPx(tst + displacePerp * ts, 8.0)).r;
        mediump vec2 toff = displaceDir * (tn1 - 0.5) + displacePerp * (tn2 - 0.5);
        mediump float h = texture2D(maskB,
                (floor((uv + toff * touchStr) * hRes) + 0.5) / hRes).a;

        float nt = timeY * noiseDirChange;
        float ni = floor(nt);
        float nf = smoothstep01(fract(nt));
        vec2 nd = mix(hash2Scalar(vec2(ni)), hash2Scalar(vec2(ni + 1.0)), nf);
        vec2 nm = texture2D(noiseMap, uv * vec2(1.5, 1.46) + nd * noiseEvoSpeed).xy - 0.5;

        float q = smoothstep01((h - (noiseRadius - noiseSmooth)) / (2.0 * noiseSmooth));

        float cellPx = screenSize.y * sdfCellSize;
        vec2 gp = (uv + nm * 2.0 * noiseDispStr * cellPx / screenSize) * screenSize / cellPx;
        vec2 gcell = floor(gp);
        vec2 gf = fract(gp);
        float mt = timeY * sdfMoveSpeed;
        float mi = floor(mt);
        float mf = smoothstep01(fract(mt));
        vec2 ca = gcell + vec2(mi);
        vec2 pa = vec2(gridHash1(ca), gridHash1(ca + vec2(17.1700001)));
        vec2 pb = vec2(gridHash1(ca + vec2(1.0)), gridHash1(ca + vec2(18.1700001)));
        vec2 pt = mix(pa, pb, mf);
        float s2 = smoothstep01((length(gf - pt) - sdfFalloff) / sdfSmooth);
        vec3 dotCol = clamp(s2 * touchGlow + s2, 0.0, 1.0);

        vec3 tGlow = smoothstep01(clamp(h, 0.0, 1.0)) * touchGlow;
        vec3 a9 = q * dotCol;
        vec3 c13 = a9 * tGlow;
        vec3 c14 = 2.0 * c13;
        vec3 c8 = 1.0 - 2.0 * (1.0 - a9) * (1.0 - tGlow);
        vec3 pick = clamp(mix(c8 - 2.0 * c13, c14, step(vec3(0.5), a9)), 0.0, 1.0);
        touchCol = tGlow * 0.5 + pick;
    }

    vec3 col = touchCol + shine + body + zoneUnder * (1.0 - alphaOut);

    gl_FragColor = vec4(col, 1.0);
}
