#version 330 core
uniform vec2 iResolution;
uniform float iTime;
in vec2 uv;
out vec4 FragColor;

// otaviogood's spiral noise
const float nudge = 0.9;
float normalizer = 1.0 / sqrt(1.0 + nudge*nudge);
float SpiralNoiseC(vec3 p)
{
    float n = 0.0;
    float iter = 2.0;
    for (int i = 0; i < 8; i++)
    {
        n += -abs(sin(p.y*iter) + cos(p.x*iter)) / iter;
        p.xy += vec2(p.y, -p.x) * nudge;
        p.xy *= normalizer;
        p.xz += vec2(p.z, -p.x) * nudge;
        p.xz *= normalizer;
        iter *= 1.733733;
    }
    return n;
}
float hash(float n) {
    return fract(sin(n) * 43758.5453);
}

float noise(vec2 p) {
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    fp = fp * fp * (3.0 - 2.0 * fp);

    float a = hash(ip.x + ip.y * 57.0);
    float b = hash(ip.x + 1.0 + ip.y * 57.0);
    float c = hash(ip.x + (ip.y + 1.0) * 57.0);
    float d = hash(ip.x + 1.0 + (ip.y + 1.0) * 57.0);

    return mix(mix(a, b, fp.x), mix(c, d, fp.x), fp.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for(int i = 0; i < 6; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

void main() {
    vec2 fragCoord = uv * iResolution;
    vec2 uv = fragCoord.xy / iResolution.xy;
    uv = uv * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;

    // Domain warping for more organic shapes
    vec2 warped_uv = uv + vec2(fbm(uv * 3.0 + iTime * 0.05), fbm(uv * 3.0 + 100.0 - iTime * 0.03)) * 0.3;

    // Vary detail levels across the nebula
    float detail_scale = fbm(warped_uv * 0.3 + iTime * 0.01) * 1.4 + 0.3;

    // Create nebula layers
    float nebula1 = fbm(warped_uv * 2.0 * detail_scale + iTime * 0.1);
    float nebula2 = fbm(warped_uv * 3.0 * detail_scale - iTime * 0.05);
    float nebula3 = fbm(warped_uv * 1.5 * detail_scale + vec2(iTime * 0.02, iTime * 0.03));
    float nebula4 = fbm(warped_uv * 0.8 * detail_scale + iTime * 0.08);

    // Add spiral structures
    float spiral_density = SpiralNoiseC(vec3(warped_uv * 0.5, iTime * 0.1)) * 0.3;

    // Combine layers
    float density = nebula1 * 0.2 + nebula2 * 0.2 + nebula3 * 0.2 + nebula4 * 0.2 + spiral_density;
    density = clamp(density, 0.0, 1.0);

    // Add dark dust lanes
    float dark_lanes = fbm(warped_uv * 8.0 + iTime * 0.02) * 0.01;
    density = max(0.0, density - dark_lanes);
    // Carina Nebula color palette
    vec3 color1 = vec3(0.2, 0.4, 1.0); // Light Blue
    vec3 color2 = vec3(1.0, 0.6, 0.2); // Light Orange
    vec3 color3 = vec3(1.0, 1.0, 0.4); // Light Yellow
    vec3 color4 = vec3(0.7, 0.4, 0.2); // Light Brown
    vec3 color5 = vec3(1.0, 0.2, 0.2); // Light Red

    // Mix colors based on density and position
    vec3 color = mix(color1, color2, density);
    float t1 = fbm(warped_uv * 4.0 + iTime * 0.15);
    color = mix(color, color3, t1);
    float t2 = fbm(warped_uv * 5.0 - iTime * 0.08);
    color = mix(color, color4, t2);
    float t3 = fbm(warped_uv * 6.0 + iTime * 0.12);
    color = mix(color, color5, t3 * 0.4);

    // Add some brightness variation
    float brightness = pow(density, 2.0) * 16.0;
    color *= brightness;

    // Add white to bright areas for definition
    color = mix(color, vec3(1.0), step(0.8, density));

    // Add base color variation for lighter dark areas
    vec3 base_color = mix(color1 * 0.1, color2 * 0.1, fbm(warped_uv * 2.0 + iTime * 0.1));
    base_color += color3 * fbm(warped_uv * 3.0 + iTime * 0.05) * 0.1;
    color += base_color;

    // Add a subtle glow
    color += vec3(1.0, 1.0, 1.0) * density * 0.3;

    // Add starfield
    float threshold = 0.8 + fbm(uv * 100.0) * 0.1;
    float star_val = step(threshold, fbm(uv * 250.0));
    float star_brightness = 0.5 + fbm(uv * 150.0) * 0.5;
    float color_hue = fbm(uv * 100.0 + 123.0);
    float color_variation = fbm(uv * 100.0 + 456.0);
    vec3 star_color = mix(vec3(0.2, 0.4, 1.0), vec3(1.0, 1.0, 0.2), color_hue); // Blue to Yellow
    star_color = mix(star_color, vec3(1.0, 0.1, 0.1), color_variation * 0.7); // Add Red
    vec3 stars = star_val * star_brightness * star_color * 2.0;

    FragColor = vec4(color + stars, 1.0);
}