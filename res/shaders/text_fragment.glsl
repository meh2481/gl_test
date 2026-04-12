#version 450

// Push constants (same layout as vertex shader; only width/height used for fwidth)
layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
} pc;

// Per-string glyph descriptor: one entry per visible (outline) glyph in the string.
// Encodes where in the per-string flat contour array this glyph's data lives.
struct GpuGlyphDesc {
    uint contourOffset; // first contour index in the per-string flat contour array
    uint numContours;   // number of contours for this glyph
    uint pad0;
    uint pad1;
};
layout(std430, set = 0, binding = 0) readonly buffer GlyphDescBuffer {
    GpuGlyphDesc descs[];
} glyphDescBuf;

// Per-string flat contour array (no header prefix; indexed by contourOffset above).
// segmentOffset is a global index into the per-string segment array below.
struct GpuTextContour {
    uint numSegments;
    int  winding;       // +1 outer, -1 hole
    uint segmentOffset; // first index into the per-string segment array
    uint pad;
};
layout(std430, set = 0, binding = 1) readonly buffer TextContourBuffer {
    GpuTextContour contours[];
} textContourBuf;

// Per-string flat segment array (cubic Bézier control points, 4×vec2 each).
struct GpuTextSegment {
    vec2 p0, p1, p2, p3;
};
layout(std430, set = 0, binding = 2) readonly buffer TextSegmentBuffer {
    GpuTextSegment segments[];
} textSegBuf;

layout(location = 0) in  vec2      fragLocalPos;
layout(location = 1) in  flat uint fragGlyphIdx;
layout(location = 2) in  vec4      fragColor;

layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------
// Distance from point P to the quadratic Bézier Q(t)=(1-t)²A + 2(1-t)tB + t²C.
// (Same implementation as vector_fragment.glsl)
// ---------------------------------------------------------------------------
float sdQuadBezier(vec2 pos, vec2 A, vec2 B, vec2 C) {
    vec2 a = B - A;
    vec2 b = A - 2.0*B + C;
    vec2 d = A - pos;

    float bb = dot(b, b);
    float aa = dot(a, a);
    if (bb < 1e-8 * (aa + 1e-4)) {
        if (aa < 1e-10) return length(d);
        float t = clamp(-dot(d, a) / (2.0 * aa), 0.0, 1.0);
        return length(d + 2.0 * a * t);
    }

    float kk = 1.0 / bb;
    float kx = kk * dot(a, b);
    float ky = kk * (2.0*dot(a,a) + dot(d,b)) / 3.0;
    float kz = kk * dot(d, a);

    float p  = ky - kx*kx;
    float p3 = p*p*p;
    float q  = kx*(2.0*kx*kx - 3.0*ky) + kz;
    float h  = q*q + 4.0*p3;

    float res;
    if (h >= 0.0) {
        h = sqrt(h);
        vec2  x  = (vec2(h, -h) - q) / 2.0;
        vec2  uv = sign(x) * pow(abs(x), vec2(1.0/3.0));
        float t  = clamp(uv.x + uv.y - kx, 0.0, 1.0);
        vec2  qp = d + (2.0*a + b*t)*t;
        res = dot(qp, qp);
    } else {
        float z = sqrt(-p);
        float v = acos(clamp(q / (p*z*2.0), -1.0, 1.0)) / 3.0;
        float m = cos(v);
        float n = sin(v) * 1.732050808;
        vec3  tv = clamp(vec3(m+m, -n-m, n-m)*z - kx, 0.0, 1.0);
        float da = dot(d+(2.0*a+b*tv.x)*tv.x, d+(2.0*a+b*tv.x)*tv.x);
        float db = dot(d+(2.0*a+b*tv.y)*tv.y, d+(2.0*a+b*tv.y)*tv.y);
        res = min(da, db);
    }
    return sqrt(res);
}

// ---------------------------------------------------------------------------
// Tangent-preserving quadratic control point for cubic p0-p1-p2-p3.
// (Same implementation as vector_fragment.glsl)
// ---------------------------------------------------------------------------
vec2 cubicQuadB(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    vec2 d0  = p1 - p0;
    vec2 d1  = p2 - p3;
    float det = d0.y * d1.x - d0.x * d1.y;
    if (det * det <= 1e-4 * dot(d0, d0) * dot(d1, d1)) return (p1 + p2) * 0.5;
    vec2  r = p3 - p0;
    float s = (r.y * d1.x - r.x * d1.y) / det;
    return p0 + s * d0;
}

// ---------------------------------------------------------------------------
// Signed winding-number contribution of one quadratic Bézier arc A-B-C.
// (Same implementation as vector_fragment.glsl)
// ---------------------------------------------------------------------------
int windingQuad(vec2 pos, vec2 A, vec2 B, vec2 C) {
    float qa = A.y - 2.0*B.y + C.y;
    float qb = 2.0*(B.y - A.y);
    float qc = A.y - pos.y;
    int w = 0;
    if (abs(qa) < 1e-7) {
        if (abs(qb) > 1e-7) {
            float t = -qc / qb;
            if (t > 0.0 && t <= 1.0) {
                float qx = (A.x - 2.0*B.x + C.x)*t*t + 2.0*(B.x - A.x)*t + A.x;
                if (qx > pos.x) {
                    w += (A.y < pos.y) ? 1 : -1;
                }
            }
        }
    } else {
        float disc = qb*qb - 4.0*qa*qc;
        if (disc >= 0.0) {
            float sq = sqrt(disc);
            for (int s = -1; s <= 1; s += 2) {
                float t = (-qb + float(s)*sq) / (2.0*qa);
                if (t > 0.0 && t <= 1.0) {
                    float qx = (A.x - 2.0*B.x + C.x)*t*t + 2.0*(B.x - A.x)*t + A.x;
                    if (qx > pos.x) {
                        float dy = 2.0*qa*t + qb;
                        w += (dy > 0.0) ? 1 : -1;
                    }
                }
            }
        }
    }
    return w;
}

// ---------------------------------------------------------------------------
// Evaluate SDF + winding for a cubic Bézier (de Casteljau split at t=0.5).
// (Same implementation as vector_fragment.glsl)
// ---------------------------------------------------------------------------
void evalCubicSDF(vec2 pos, vec2 p0, vec2 p1, vec2 p2, vec2 p3,
                  inout float minDist, inout int winding) {
    vec2 m01  = (p0  + p1)  * 0.5;
    vec2 m12  = (p1  + p2)  * 0.5;
    vec2 m23  = (p2  + p3)  * 0.5;
    vec2 m012 = (m01 + m12) * 0.5;
    vec2 m123 = (m12 + m23) * 0.5;
    vec2 mid  = (m012 + m123) * 0.5;

    vec2 B1 = cubicQuadB(p0, m01, m012, mid);
    minDist = min(minDist, sdQuadBezier(pos, p0, B1, mid));
    winding += windingQuad(pos, p0, B1, mid);

    vec2 B2 = cubicQuadB(mid, m123, m23, p3);
    minDist = min(minDist, sdQuadBezier(pos, mid, B2, p3));
    winding += windingQuad(pos, mid, B2, p3);
}

void main() {
    GpuGlyphDesc desc = glyphDescBuf.descs[fragGlyphIdx];

    float minDist     = 1e9;
    int   totalWinding = 0;

    for (uint ci = 0u; ci < desc.numContours; ci++) {
        GpuTextContour c = textContourBuf.contours[desc.contourOffset + ci];
        for (uint si = 0u; si < c.numSegments; si++) {
            GpuTextSegment seg = textSegBuf.segments[c.segmentOffset + si];
            evalCubicSDF(fragLocalPos, seg.p0, seg.p1, seg.p2, seg.p3,
                         minDist, totalWinding);
        }
    }

    bool  inside = (totalWinding != 0);
    float sdf    = inside ? -minDist : minDist;

    float aa    = fwidth(sdf) * 0.75;
    float alpha = smoothstep(aa, -aa, sdf);

    if (alpha < 0.001) discard;
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
