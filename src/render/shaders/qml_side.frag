#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D encodedTexture;
layout(binding = 2) uniform sampler2D yTexture;
layout(binding = 3) uniform sampler2D uTexture;
layout(binding = 4) uniform sampler2D vTexture;
layout(binding = 5) uniform sampler2D rawTexture;

layout(std140, binding = 6) uniform YuvParameters {
    vec4 coefficients;
    vec4 ranges;
    vec4 yuvFlags;
    mat3 yuvPrimaryToDisplay;
    vec4 yuvColorGeometry; // chroma location, width, height, display transfer
};

layout(std140, binding = 7) uniform BayerParameters {
    vec4 imageAndStorage;
    vec4 layoutFlags;
    vec4 levelsAndGamma;
    vec4 whiteBalance;
    mat3 colorCorrection;
};

layout(std140, binding = 8) uniform SourceParameters {
    vec4 sourceParameters; // 0 = encoded, 1 = YUV, 2 = Bayer
};

vec2 orientedUv(vec2 displayUv, float orientation) {
    if (orientation > 0.5 && orientation < 1.5)
        return vec2(displayUv.y, 1.0 - displayUv.x);
    if (orientation > 1.5 && orientation < 2.5)
        return vec2(1.0 - displayUv.x, 1.0 - displayUv.y);
    if (orientation > 2.5)
        return vec2(1.0 - displayUv.y, displayUv.x);
    return displayUv;
}

float decodeYuvTransfer(float value) {
    value = clamp(value, 0.0, 1.0);
    int transfer = int(round(yuvFlags.w));
    if (transfer == 2) return value;
    if (transfer == 1)
        return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
    return value < 0.081 ? value / 4.5 : pow((value + 0.099) / 1.099, 1.0 / 0.45);
}

// Mirrors encodeForDisplay() in core/color_conversion.cpp so the GPU buffer and the CPU
// reference paths describe the same display space.
float encodeDisplay(float value) {
    value = clamp(value, 0.0, 1.0);
    int transfer = int(round(yuvColorGeometry.w));
    if (transfer == 1) return pow(value, 1.0 / 2.19921875);
    if (transfer == 2)
        return value <= 0.0181 ? 4.5 * value : 1.0993 * pow(value, 1.0 / 2.2) - 0.0993;
    return value <= 0.0031308 ? 12.92 * value
                              : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

vec4 sampleYuv() {
    vec2 sourceUv = orientedUv(uv, yuvFlags.z);
    float y = texture(yTexture, sourceUv).r;
    vec2 chromaUv = sourceUv;
    if (yuvColorGeometry.x > 0.5)
        chromaUv.x += 0.5 / max(yuvColorGeometry.y, 1.0);
    vec2 chroma;
    if (yuvFlags.x > 0.5) {
        chroma = vec2(texture(uTexture, chromaUv).r, texture(vTexture, chromaUv).r);
    } else {
        chroma = texture(uTexture, chromaUv).rg;
        if (yuvFlags.y > 0.5) chroma = chroma.yx;
    }
    float luma = (y - ranges.x) / ranges.y;
    vec2 centered = (chroma - vec2(ranges.z)) / ranges.w;
    vec3 rgb = vec3(luma + coefficients.x * centered.y,
                    luma - coefficients.y * centered.x - coefficients.z * centered.y,
                    luma + coefficients.w * centered.x);
    vec3 linear = vec3(decodeYuvTransfer(rgb.r), decodeYuvTransfer(rgb.g),
                       decodeYuvTransfer(rgb.b));
    vec3 displayLinear = yuvPrimaryToDisplay * linear;
    return vec4(encodeDisplay(displayLinear.r), encodeDisplay(displayLinear.g),
                encodeDisplay(displayLinear.b), 1.0);
}

int byteAt(int byteX, int y) {
    return int(round(texelFetch(rawTexture, ivec2(byteX, y), 0).r * 255.0));
}

int rawValueAt(ivec2 pixel) {
    int format = int(round(imageAndStorage.w));
    int x = pixel.x;
    int y = pixel.y;
    if (format == 0) {
        int group = (x / 4) * 5;
        int lane = x % 4;
        return (byteAt(group + lane, y) << 2) |
               ((byteAt(group + 4, y) >> (lane * 2)) & 3);
    }
    if (format == 1) {
        int group = (x / 2) * 3;
        int lane = x % 2;
        return (byteAt(group + lane, y) << 4) |
               ((byteAt(group + 2, y) >> (lane * 4)) & 15);
    }
    int offset = x * 2;
    int first = byteAt(offset, y);
    int second = byteAt(offset + 1, y);
    int value = layoutFlags.y > 0.5 ? (first | (second << 8)) : ((first << 8) | second);
    int validBits = int(round(layoutFlags.w));
    if (validBits < 16)
        value = layoutFlags.z > 0.5 ? value >> (16 - validBits)
                                    : value & ((1 << validBits) - 1);
    return value;
}

int cfaChannel(ivec2 pixel) {
    int blockSize = int(round(imageAndStorage.z));
    bool evenX = ((pixel.x / blockSize) & 1) == 0;
    bool evenY = ((pixel.y / blockSize) & 1) == 0;
    int pattern = int(round(layoutFlags.x));
    if (pattern == 0) return evenY ? (evenX ? 0 : 1) : (evenX ? 1 : 2);
    if (pattern == 1) return evenY ? (evenX ? 1 : 0) : (evenX ? 2 : 1);
    if (pattern == 2) return evenY ? (evenX ? 1 : 2) : (evenX ? 0 : 1);
    return evenY ? (evenX ? 2 : 1) : (evenX ? 1 : 0);
}

float normalizedRaw(ivec2 pixel) {
    return clamp((float(rawValueAt(pixel)) - levelsAndGamma.x) /
                 (levelsAndGamma.y - levelsAndGamma.x), 0.0, 1.0);
}

vec4 sampleBayer() {
    ivec2 imageSize = ivec2(round(imageAndStorage.xy));
    vec2 sourceUv = orientedUv(uv, levelsAndGamma.w);
    ivec2 center = clamp(ivec2(floor(sourceUv * vec2(imageSize))),
                         ivec2(0), imageSize - ivec2(1));
    if (whiteBalance.w < 0.5) {
        int validBits = int(round(layoutFlags.w));
        float encoded = float(rawValueAt(center)) / float((1 << validBits) - 1);
        int channel = cfaChannel(center);
        return vec4(channel == 0 ? encoded : 0.0,
                    channel == 1 ? encoded : 0.0,
                    channel == 2 ? encoded : 0.0, 1.0);
    }
    vec3 sums = vec3(0.0);
    vec3 counts = vec3(0.0);
    int radius = int(round(imageAndStorage.z));
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (abs(dx) > radius || abs(dy) > radius) continue;
            ivec2 samplePixel = clamp(center + ivec2(dx, dy), ivec2(0), imageSize - ivec2(1));
            int channel = cfaChannel(samplePixel);
            float value = normalizedRaw(samplePixel);
            if (channel == 0) { sums.x += value; counts.x += 1.0; }
            else if (channel == 1) { sums.y += value; counts.y += 1.0; }
            else { sums.z += value; counts.z += 1.0; }
        }
    }
    vec3 balanced = (sums / max(counts, vec3(1.0))) * whiteBalance.rgb;
    vec3 corrected = clamp(colorCorrection * balanced, 0.0, 1.0);
    return vec4(pow(corrected, vec3(1.0 / levelsAndGamma.z)), 1.0);
}

void main() {
    int sourceType = int(round(sourceParameters.x));
    if (sourceType == 1) {
        fragColor = sampleYuv();
        return;
    }
    if (sourceType == 2) {
        fragColor = sampleBayer();
        return;
    }
    vec4 source = texture(encodedTexture, uv);
    vec2 cell = floor(gl_FragCoord.xy / 10.0);
    vec3 background = mix(vec3(0.20), vec3(0.28), mod(cell.x + cell.y, 2.0));
    fragColor = vec4(mix(background, source.rgb, source.a), 1.0);
}
