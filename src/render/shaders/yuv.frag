#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D yTexture;
layout(binding = 2) uniform sampler2D uTexture;
layout(binding = 3) uniform sampler2D vTexture;
layout(std140, binding = 4) uniform YuvParameters {
    vec4 coefficients; // redV, greenU, greenV, blueU
    vec4 ranges;       // yOffset, yScale, chromaCenter, chromaScale
    vec4 flags;        // planar, swapUV, orientation, source transfer function
    mat3 primaryToDisplay;
    vec4 colorGeometry; // chroma location, source width, source height, display transfer
};

float decodeTransfer(float value) {
    value = clamp(value, 0.0, 1.0);
    int transfer = int(round(flags.w));
    if (transfer == 2) return value;
    if (transfer == 1)
        return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
    return value < 0.081 ? value / 4.5 : pow((value + 0.099) / 1.099, 1.0 / 0.45);
}

// Mirrors encodeForDisplay() in core/color_conversion.cpp so the GPU buffer and the CPU
// reference paths describe the same display space.
float encodeDisplay(float value) {
    value = clamp(value, 0.0, 1.0);
    int transfer = int(round(colorGeometry.w));
    if (transfer == 1) return pow(value, 1.0 / 2.19921875);
    if (transfer == 2)
        return value <= 0.0181 ? 4.5 * value : 1.0993 * pow(value, 1.0 / 2.2) - 0.0993;
    return value <= 0.0031308 ? 12.92 * value
                              : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

vec2 sourceUvForOrientation(vec2 displayUv) {
    float orientation = flags.z;
    if (orientation > 0.5 && orientation < 1.5) {
        return vec2(displayUv.y, 1.0 - displayUv.x);
    }
    if (orientation > 1.5 && orientation < 2.5) {
        return vec2(1.0 - displayUv.x, 1.0 - displayUv.y);
    }
    if (orientation > 2.5) {
        return vec2(1.0 - displayUv.y, displayUv.x);
    }
    return displayUv;
}

void main() {
    vec2 sourceUv = sourceUvForOrientation(uv);
    float y = texture(yTexture, sourceUv).r;
    vec2 chromaUv = sourceUv;
    if (colorGeometry.x > 0.5)
        chromaUv.x += 0.5 / max(colorGeometry.y, 1.0);
    vec2 chroma;
    if (flags.x > 0.5) {
        chroma = vec2(texture(uTexture, chromaUv).r, texture(vTexture, chromaUv).r);
    } else {
        chroma = texture(uTexture, chromaUv).rg;
        if (flags.y > 0.5) {
            chroma = chroma.yx;
        }
    }
    float luma = (y - ranges.x) / ranges.y;
    vec2 centered = (chroma - vec2(ranges.z)) / ranges.w;
    vec3 rgb = vec3(
        luma + coefficients.x * centered.y,
        luma - coefficients.y * centered.x - coefficients.z * centered.y,
        luma + coefficients.w * centered.x
    );
    vec3 linear = vec3(decodeTransfer(rgb.r), decodeTransfer(rgb.g), decodeTransfer(rgb.b));
    vec3 displayLinear = primaryToDisplay * linear;
    fragColor = vec4(encodeDisplay(displayLinear.r), encodeDisplay(displayLinear.g),
                     encodeDisplay(displayLinear.b), 1.0);
}
