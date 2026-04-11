#version 450

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
} pc;

layout(location = 0) in vec3 fragKlm;
layout(location = 1) in float fragSign;

layout(location = 0) out vec4 outColor;

void main() {
    if (abs(fragSign) < 0.5) {
        // Solid-fill triangle: always render
        outColor = vec4(pc.r, pc.g, pc.b, pc.a);
        return;
    }

    // Loop-Blinn implicit surface test: f = k^3 - l*m
    // Vertices carry (k,l,m) = (0,0,0), (0.5,0,0.5), (1,1,1) for quadratic approximation.
    // f = 0 on the parabola boundary; f < 0 is inside, f > 0 is outside.
    float f = fragKlm.x * fragKlm.x * fragKlm.x - fragKlm.y * fragKlm.z;
    float fw = fwidth(f);
    // fragSign = +1: keep f <= 0 region (convex outward bump added to fill)
    // fragSign = -1: keep f >= 0 region (concave notch removed from fill)
    // The 0.5 offset centres the smoothstep transition on the boundary f=0.
    // The 1e-6 guard prevents division-by-zero when fwidth returns 0.
    float alpha = clamp(0.5 - fragSign * f / (fw + 1e-6), 0.0, 1.0);
    if (alpha < 0.001) discard;
    outColor = vec4(pc.r, pc.g, pc.b, pc.a * alpha);
}
