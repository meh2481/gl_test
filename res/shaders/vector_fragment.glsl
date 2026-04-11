#version 450

// Push constants (same layout as vertex shader)
layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float modelX;
    float modelY;
    float modelScale;
    float r;
    float g;
    float b;
    float a;
    float bboxMinX;
    float bboxMinY;
    float bboxMaxX;
    float bboxMaxY;
} pc;

// Per-shape SSBO: contour metadata (numSegments, winding, segmentOffset)
struct GpuContour {
    uint numSegments;
    int  winding;       // +1 outer, -1 hole
    uint segmentOffset; // first index into segments[]
    uint pad;
};
layout(std430, set = 0, binding = 0) readonly buffer ContourBuffer {
    uint       numContours;
    uint       pad0[3];
    GpuContour contours[];
} contourBuf;

// Per-shape SSBO: flat cubic Bézier segments (8 floats each)
struct GpuSegment {
    vec2 p0, p1, p2, p3;
};
layout(std430, set = 0, binding = 1) readonly buffer SegmentBuffer {
    GpuSegment segments[];
} segBuf;

layout(location = 0) in  vec2 fragLocalPos;
layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------
// Distance from point P to the quadratic Bézier Q(t)=(1-t)²A + 2(1-t)tB + t²C.
// Returns the unsigned minimum distance.
// Based on Inigo Quilez's analytic formula (MIT-licensed, iquilezles.org).
// ---------------------------------------------------------------------------
float sdQuadBezier(vec2 pos, vec2 A, vec2 B, vec2 C) {
    vec2 a = B - A;
    vec2 b = A - 2.0*B + C;
    vec2 d = A - pos;

    float kk = 1.0 / dot(b, b);
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
// Signed winding-number contribution of one quadratic Bézier arc A-B-C
// for a rightward (+x) ray from point P.
// Returns +1 (upward crossing), -1 (downward crossing), or 0.
// ---------------------------------------------------------------------------
int windingQuad(vec2 pos, vec2 A, vec2 B, vec2 C) {
    // Solve Q_y(t) = pos.y  for  t in (0,1]
    // Q_y(t) = (A.y - 2B.y + C.y)t² + 2(B.y - A.y)t + A.y
    float qa = A.y - 2.0*B.y + C.y;
    float qb = 2.0*(B.y - A.y);
    float qc = A.y - pos.y;
    int w = 0;
    if (abs(qa) < 1e-7) {
        // Degenerate to linear
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
                        // Direction of crossing: derivative of Q_y at t
                        float dy = 2.0*qa*t + qb;
                        w += (dy > 0.0) ? 1 : -1;
                    }
                }
            }
        }
    }
    return w;
}

void main() {
    vec2 pos = fragLocalPos;

    float minDist = 1e9;
    int   totalWinding = 0;

    uint nc = contourBuf.numContours;
    for (uint ci = 0; ci < nc; ci++) {
        GpuContour c = contourBuf.contours[ci];
        for (uint si = 0; si < c.numSegments; si++) {
            GpuSegment seg = segBuf.segments[c.segmentOffset + si];
            // Approximate cubic as quadratic by averaging the two interior control points.
            // Trade-off: the midpoint approximation introduces a slight bulge/pinch on
            // high-curvature cubics (inflection-point curves), but is visually
            // indistinguishable at typical display sizes and avoids solving a degree-6
            // polynomial. A Newton-step refinement can be added in a future pass.
            vec2 A = seg.p0;
            vec2 B = (seg.p1 + seg.p2) * 0.5;
            vec2 C = seg.p3;

            float d = sdQuadBezier(pos, A, B, C);
            if (d < minDist) minDist = d;

            totalWinding += windingQuad(pos, A, B, C);
        }
    }

    // Non-zero fill rule: inside if winding != 0
    bool inside = (totalWinding != 0);
    float sdf = inside ? -minDist : minDist;

    // Anti-aliased alpha from SDF, using screen-space derivative for pixel width.
    float aa = fwidth(sdf) * 0.75;
    float alpha = smoothstep(aa, -aa, sdf);

    if (alpha < 0.001) discard;
    outColor = vec4(pc.r, pc.g, pc.b, pc.a * alpha);
}
