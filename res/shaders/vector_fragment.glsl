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
    // For quadratic approximation (m set to k for homogeneous coord):
    //   vertices carry (k,l,m) = (0,0,0), (0.5,0,0.5), (1,1,1)
    //   so f interpolates as k^3 - l*m
    float f = fragKlm.x * fragKlm.x * fragKlm.x - fragKlm.y * fragKlm.z;
    float fw = fwidth(f);
    // fragSign > 0: keep where f <= 0 (inside parabola, adds convex bump)
    // fragSign < 0: keep where f >= 0 (outside parabola, concave notch)
    float alpha = clamp(0.5 - fragSign * f / (fw + 1e-6), 0.0, 1.0);
    if (alpha < 0.001) discard;
    outColor = vec4(pc.r, pc.g, pc.b, pc.a * alpha);
}
