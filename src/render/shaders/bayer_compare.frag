#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D primaryRawTexture;
layout(binding = 2) uniform sampler2D candidateRawTexture;
layout(std140, binding = 3) uniform PrimaryBayerParameters {
    vec4 primaryImageAndStorage;
    vec4 primaryLayoutFlags;
    vec4 primaryLevelsAndGamma;
    vec4 primaryWhiteBalance;
    mat3 primaryColorCorrection;
};
layout(std140, binding = 4) uniform CandidateBayerParameters {
    vec4 candidateImageAndStorage;
    vec4 candidateLayoutFlags;
    vec4 candidateLevelsAndGamma;
    vec4 candidateWhiteBalance;
    mat3 candidateColorCorrection;
};
layout(std140, binding = 5) uniform CompareParameters {
    vec4 compareParams;       // mode, split position, unused, unused
    vec4 comparisonTransform; // reserved
};

int primaryByteAt(int byteX, int y) {
    return int(round(texelFetch(primaryRawTexture, ivec2(byteX, y), 0).r * 255.0));
}

int candidateByteAt(int byteX, int y) {
    return int(round(texelFetch(candidateRawTexture, ivec2(byteX, y), 0).r * 255.0));
}

int unpackPrimary(ivec2 pixel) {
    int format = int(round(primaryImageAndStorage.w));
    if (format == 0) {
        int group = (pixel.x / 4) * 5;
        int lane = pixel.x % 4;
        return (primaryByteAt(group + lane, pixel.y) << 2) |
               ((primaryByteAt(group + 4, pixel.y) >> (lane * 2)) & 3);
    }
    if (format == 1) {
        int group = (pixel.x / 2) * 3;
        int lane = pixel.x % 2;
        return (primaryByteAt(group + lane, pixel.y) << 4) |
               ((primaryByteAt(group + 2, pixel.y) >> (lane * 4)) & 15);
    }
    int offset = pixel.x * 2;
    int first = primaryByteAt(offset, pixel.y);
    int second = primaryByteAt(offset + 1, pixel.y);
    int value = primaryLayoutFlags.y > 0.5 ? (first | (second << 8)) : ((first << 8) | second);
    int validBits = int(round(primaryLayoutFlags.w));
    if (validBits < 16) {
        value = primaryLayoutFlags.z > 0.5 ? value >> (16 - validBits)
                                           : value & ((1 << validBits) - 1);
    }
    return value;
}

int unpackCandidate(ivec2 pixel) {
    int format = int(round(candidateImageAndStorage.w));
    if (format == 0) {
        int group = (pixel.x / 4) * 5;
        int lane = pixel.x % 4;
        return (candidateByteAt(group + lane, pixel.y) << 2) |
               ((candidateByteAt(group + 4, pixel.y) >> (lane * 2)) & 3);
    }
    if (format == 1) {
        int group = (pixel.x / 2) * 3;
        int lane = pixel.x % 2;
        return (candidateByteAt(group + lane, pixel.y) << 4) |
               ((candidateByteAt(group + 2, pixel.y) >> (lane * 4)) & 15);
    }
    int offset = pixel.x * 2;
    int first = candidateByteAt(offset, pixel.y);
    int second = candidateByteAt(offset + 1, pixel.y);
    int value = candidateLayoutFlags.y > 0.5 ? (first | (second << 8)) : ((first << 8) | second);
    int validBits = int(round(candidateLayoutFlags.w));
    if (validBits < 16) {
        value = candidateLayoutFlags.z > 0.5 ? value >> (16 - validBits)
                                             : value & ((1 << validBits) - 1);
    }
    return value;
}

int cfaChannel(ivec2 pixel, int pattern, int blockSize) {
    bool evenX = ((pixel.x / blockSize) & 1) == 0;
    bool evenY = ((pixel.y / blockSize) & 1) == 0;
    if (pattern == 0) {
        return evenY ? (evenX ? 0 : 1) : (evenX ? 1 : 2);
    }
    if (pattern == 1) {
        return evenY ? (evenX ? 1 : 0) : (evenX ? 2 : 1);
    }
    if (pattern == 2) {
        return evenY ? (evenX ? 1 : 2) : (evenX ? 0 : 1);
    }
    return evenY ? (evenX ? 2 : 1) : (evenX ? 1 : 0);
}

vec2 sourceUvForOrientation(vec2 displayUv, float orientation) {
    int value = int(round(orientation));
    if (value == 1) {
        return vec2(displayUv.y, 1.0 - displayUv.x);
    }
    if (value == 2) {
        return vec2(1.0 - displayUv.x, 1.0 - displayUv.y);
    }
    if (value == 3) {
        return vec2(1.0 - displayUv.y, displayUv.x);
    }
    return displayUv;
}

ivec2 primaryPixel(vec2 displayUv) {
    ivec2 size = ivec2(round(primaryImageAndStorage.xy));
    vec2 sourceUv = sourceUvForOrientation(displayUv, primaryLevelsAndGamma.w);
    return clamp(ivec2(floor(sourceUv * vec2(size))), ivec2(0), size - ivec2(1));
}

ivec2 candidatePixel(vec2 displayUv) {
    ivec2 size = ivec2(round(candidateImageAndStorage.xy));
    vec2 sourceUv = sourceUvForOrientation(displayUv, candidateLevelsAndGamma.w);
    return clamp(ivec2(floor(sourceUv * vec2(size))), ivec2(0), size - ivec2(1));
}

float primaryDisplayValue(ivec2 pixel) {
    float denominator = primaryLevelsAndGamma.y - primaryLevelsAndGamma.x;
    return clamp((float(unpackPrimary(pixel)) - primaryLevelsAndGamma.x) / denominator, 0.0, 1.0);
}

float candidateDisplayValue(ivec2 pixel) {
    float denominator = candidateLevelsAndGamma.y - candidateLevelsAndGamma.x;
    return clamp((float(unpackCandidate(pixel)) - candidateLevelsAndGamma.x) / denominator, 0.0,
                 1.0);
}

vec3 primaryDisplayRgb(vec2 displayUv) {
    ivec2 size = ivec2(round(primaryImageAndStorage.xy));
    ivec2 center = primaryPixel(displayUv);
    if (primaryWhiteBalance.w < 0.5) {
        int validBits = int(round(primaryLayoutFlags.w));
        float encoded = float(unpackPrimary(center)) / float((1 << validBits) - 1);
        int channel = cfaChannel(center, int(round(primaryLayoutFlags.x)),
                                 int(round(primaryImageAndStorage.z)));
        return vec3(channel == 0 ? encoded : 0.0,
                    channel == 1 ? encoded : 0.0,
                    channel == 2 ? encoded : 0.0);
    }
    vec3 sums = vec3(0.0);
    vec3 counts = vec3(0.0);
    int radius = int(round(primaryImageAndStorage.z));
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (abs(dx) > radius || abs(dy) > radius) continue;
            ivec2 pixel = clamp(center + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            int channel = cfaChannel(pixel, int(round(primaryLayoutFlags.x)), radius);
            float sampleValue = primaryDisplayValue(pixel);
            if (channel == 0) {
                sums.x += sampleValue;
                counts.x += 1.0;
            } else if (channel == 1) {
                sums.y += sampleValue;
                counts.y += 1.0;
            } else {
                sums.z += sampleValue;
                counts.z += 1.0;
            }
        }
    }
    vec3 balanced = (sums / max(counts, vec3(1.0))) * primaryWhiteBalance.rgb;
    vec3 corrected = clamp(primaryColorCorrection * balanced, 0.0, 1.0);
    return pow(corrected, vec3(1.0 / primaryLevelsAndGamma.z));
}

vec3 candidateDisplayRgb(vec2 displayUv) {
    ivec2 size = ivec2(round(candidateImageAndStorage.xy));
    ivec2 center = candidatePixel(displayUv);
    if (candidateWhiteBalance.w < 0.5) {
        int validBits = int(round(candidateLayoutFlags.w));
        float encoded = float(unpackCandidate(center)) / float((1 << validBits) - 1);
        int channel = cfaChannel(center, int(round(candidateLayoutFlags.x)),
                                 int(round(candidateImageAndStorage.z)));
        return vec3(channel == 0 ? encoded : 0.0,
                    channel == 1 ? encoded : 0.0,
                    channel == 2 ? encoded : 0.0);
    }
    vec3 sums = vec3(0.0);
    vec3 counts = vec3(0.0);
    int radius = int(round(candidateImageAndStorage.z));
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (abs(dx) > radius || abs(dy) > radius) continue;
            ivec2 pixel = clamp(center + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            int channel = cfaChannel(pixel, int(round(candidateLayoutFlags.x)), radius);
            float sampleValue = candidateDisplayValue(pixel);
            if (channel == 0) {
                sums.x += sampleValue;
                counts.x += 1.0;
            } else if (channel == 1) {
                sums.y += sampleValue;
                counts.y += 1.0;
            } else {
                sums.z += sampleValue;
                counts.z += 1.0;
            }
        }
    }
    vec3 balanced = (sums / max(counts, vec3(1.0))) * candidateWhiteBalance.rgb;
    vec3 corrected = clamp(candidateColorCorrection * balanced, 0.0, 1.0);
    return pow(corrected, vec3(1.0 / candidateLevelsAndGamma.z));
}

void main() {
    vec2 candidateUv = uv;
    bool candidateInside = all(greaterThanEqual(candidateUv, vec2(0.0))) &&
                           all(lessThanEqual(candidateUv, vec2(1.0)));
    vec4 source = vec4(primaryDisplayRgb(uv), 1.0);
    vec4 candidate = candidateInside ? vec4(candidateDisplayRgb(candidateUv), 1.0) : vec4(0.0);

    int mode = int(compareParams.x + 0.5);
    if (mode == 1) {
        source = uv.x <= compareParams.y ? source : candidate;
    } else if (mode == 2) {
        source = uv.y <= compareParams.y ? source : candidate;
    }
    float seamDistance = mode == 1 ? abs(uv.x - compareParams.y) / max(fwidth(uv.x), 0.000001)
                                   : abs(uv.y - compareParams.y) / max(fwidth(uv.y), 0.000001);
    if (mode != 0 && seamDistance <= 1.0) {
        fragColor = vec4(1.0);
        return;
    }

    vec2 cell = floor(gl_FragCoord.xy / 10.0);
    float checker = mod(cell.x + cell.y, 2.0);
    vec3 background = mix(vec3(0.20), vec3(0.28), checker);
    fragColor = vec4(mix(background, source.rgb, source.a), 1.0);
}
