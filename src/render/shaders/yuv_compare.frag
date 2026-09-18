#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D primaryYTexture;
layout(binding = 2) uniform sampler2D primaryUTexture;
layout(binding = 3) uniform sampler2D primaryVTexture;
layout(binding = 4) uniform sampler2D candidateYTexture;
layout(binding = 5) uniform sampler2D candidateUTexture;
layout(binding = 6) uniform sampler2D candidateVTexture;
layout(std140, binding = 7) uniform PrimaryYuvParameters {
    vec4 primaryCoefficients;
    vec4 primaryRanges;
    vec4 primaryFlags;
    mat3 primaryToDisplay;
    vec4 primaryColorGeometry; // chroma location, width, height, display transfer
};
layout(std140, binding = 8) uniform CandidateYuvParameters {
    vec4 candidateCoefficients;
    vec4 candidateRanges;
    vec4 candidateFlags;
    mat3 candidateToDisplay;
    vec4 candidateColorGeometry; // chroma location, width, height, display transfer
};
layout(std140, binding = 9) uniform CompareParameters {
    vec4 compareParams;       // mode, split position, unused, unused
    vec4 comparisonTransform; // reserved
};

vec2 sourceUvForOrientation(vec2 displayUv, float orientation) {
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

float decodeTransfer(float value, float transferValue) {
    value = clamp(value, 0.0, 1.0);
    int transfer = int(round(transferValue));
    if (transfer == 2) return value;
    if (transfer == 1)
        return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
    return value < 0.081 ? value / 4.5 : pow((value + 0.099) / 1.099, 1.0 / 0.45);
}

// Mirrors encodeForDisplay() in core/color_conversion.cpp so the GPU buffer and the CPU
// reference paths describe the same display space.
float encodeDisplay(float value, float transferValue) {
    value = clamp(value, 0.0, 1.0);
    int transfer = int(round(transferValue));
    if (transfer == 1) return pow(value, 1.0 / 2.19921875);
    if (transfer == 2)
        return value <= 0.0181 ? 4.5 * value : 1.0993 * pow(value, 1.0 / 2.2) - 0.0993;
    return value <= 0.0031308 ? 12.92 * value
                              : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

vec3 yuvToRgb(vec3 codeValues, vec4 coefficients, vec4 ranges, vec4 flags,
              mat3 gamutMatrix, float displayTransfer) {
    float luma = (codeValues.x - ranges.x) / ranges.y;
    vec2 centered = (codeValues.yz - vec2(ranges.z)) / ranges.w;
    vec3 encoded = vec3(luma + coefficients.x * centered.y,
                        luma - coefficients.y * centered.x - coefficients.z * centered.y,
                        luma + coefficients.w * centered.x);
    vec3 linear = vec3(decodeTransfer(encoded.r, flags.w),
                       decodeTransfer(encoded.g, flags.w),
                       decodeTransfer(encoded.b, flags.w));
    vec3 displayLinear = gamutMatrix * linear;
    return vec3(encodeDisplay(displayLinear.r, displayTransfer),
                encodeDisplay(displayLinear.g, displayTransfer),
                encodeDisplay(displayLinear.b, displayTransfer));
}

vec3 samplePrimaryCode(vec2 displayUv) {
    vec2 sourceUv = sourceUvForOrientation(displayUv, primaryFlags.z);
    float y = texture(primaryYTexture, sourceUv).r;
    vec2 chromaUv = sourceUv;
    if (primaryColorGeometry.x > 0.5)
        chromaUv.x += 0.5 / max(primaryColorGeometry.y, 1.0);
    vec2 chroma;
    if (primaryFlags.x > 0.5) {
        chroma = vec2(texture(primaryUTexture, chromaUv).r,
                      texture(primaryVTexture, chromaUv).r);
    } else {
        chroma = texture(primaryUTexture, chromaUv).rg;
        if (primaryFlags.y > 0.5) {
            chroma = chroma.yx;
        }
    }
    return vec3(y, chroma);
}

vec3 sampleCandidateCode(vec2 displayUv) {
    vec2 sourceUv = sourceUvForOrientation(displayUv, candidateFlags.z);
    float y = texture(candidateYTexture, sourceUv).r;
    vec2 chromaUv = sourceUv;
    if (candidateColorGeometry.x > 0.5)
        chromaUv.x += 0.5 / max(candidateColorGeometry.y, 1.0);
    vec2 chroma;
    if (candidateFlags.x > 0.5) {
        chroma = vec2(texture(candidateUTexture, chromaUv).r,
                      texture(candidateVTexture, chromaUv).r);
    } else {
        chroma = texture(candidateUTexture, chromaUv).rg;
        if (candidateFlags.y > 0.5) {
            chroma = chroma.yx;
        }
    }
    return vec3(y, chroma);
}

void main() {
    vec2 candidateUv = uv;
    bool candidateInside = all(greaterThanEqual(candidateUv, vec2(0.0))) &&
                           all(lessThanEqual(candidateUv, vec2(1.0)));
    vec3 primaryCode = samplePrimaryCode(uv);
    vec3 candidateCode = candidateInside ? sampleCandidateCode(candidateUv) : vec3(0.0);
    vec4 source = vec4(yuvToRgb(primaryCode, primaryCoefficients, primaryRanges,
                                primaryFlags, primaryToDisplay,
                                primaryColorGeometry.w), 1.0);
    vec4 candidate = candidateInside
                         ? vec4(yuvToRgb(candidateCode, candidateCoefficients, candidateRanges,
                                        candidateFlags, candidateToDisplay,
                                        candidateColorGeometry.w), 1.0)
                         : vec4(0.0);

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
