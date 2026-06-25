// MobileGlues - gl/glsl/benchmark_parser.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

// ============================================================================
// Benchmark: Hand-written parser vs std::regex for GLSL processing
// ============================================================================
// Measures the performance difference between regex-based and manual parsing
// for common GLSL conversion operations:
//   1. getGLSLVersion  – version extraction
//   2. replace_all     – substring replacement
//   3. removeLayoutBinding – layout(binding=N) stripping
//   4. processOutColorLocations – outColor location injection
//   5. replace_line_starting_with – line prefix removal
//
// Usage:
//   g++ -O2 -std=c++17 benchmark_parser.cpp -o benchmark_parser
//   ./benchmark_parser
// ============================================================================

#include <string>
#include <regex>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cctype>
#include <vector>

// ============================================================================
// Sample GLSL test data
// ============================================================================

static const char* k_sample_glsl = R"(#version 450 core
#extension GL_ARB_shader_texture_lod : enable

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragNormal;
layout(binding = 0) uniform sampler2D tex0;
layout(binding = 1) uniform sampler2D tex1;
layout(binding = 2, std140) uniform UBO {
    mat4 modelView;
    mat4 projection;
    vec4 color;
};

uniform vec3 lightPos = vec3(1.0, 2.0, 3.0);
uniform float ambient = 0.1;

in vec2 texCoord;
in vec3 normal;
in vec4 worldPos;

outColor0 = vec4(1.0);
outColor1 = vec4(0.0, 0.0, 1.0, 1.0);

void main() {
    vec4 texColor = texture(tex0, texCoord);
    vec4 texColor2 = texture(tex1, texCoord);
    float diff = max(dot(normal, normalize(lightPos)), 0.0);
    vec4 color = texColor * (ambient + diff);
    fragColor = color;
    fragNormal = vec4(normal, 1.0);
}
)";

// Generate a larger test string by repeating the sample N times
static std::string generate_large_shader(int repeat_count) {
    std::string result;
    result.reserve(strlen(k_sample_glsl) * repeat_count);
    for (int i = 0; i < repeat_count; i++) {
        result += k_sample_glsl;
    }
    return result;
}

// ============================================================================
// Timer utility
// ============================================================================

struct Timer {
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    TimePoint start;
    const char* name;

    Timer(const char* n) : start(Clock::now()), name(n) {}
    ~Timer() {
        auto end = Clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  " << std::left << std::setw(40) << name
                  << std::right << std::setw(10) << us << " us" << std::endl;
    }
};

// ============================================================================
// Benchmark 1: getGLSLVersion — manual scan vs regex
// ============================================================================

int getGLSLVersion_manual(const char* glsl_code) {
    const char* p = glsl_code;
    while (*p) {
        if (*p == '#' && strncmp(p, "#version", 8) == 0) {
            p += 8;
            while (*p == ' ' || *p == '\t') p++;
            if (*p >= '0' && *p <= '9') {
                int v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                return v;
            }
        }
        p++;
    }
    return -1;
}

int getGLSLVersion_regex(const std::string& glsl_code) {
    static const std::regex re(R"(#version\s+(\d+))");
    std::smatch m;
    if (std::regex_search(glsl_code, m, re)) return std::stoi(m[1].str());
    return -1;
}

// ============================================================================
// Benchmark 2: replace_all — manual vs regex
// ============================================================================

void replace_all_manual(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

void replace_all_regex(std::string& str, const std::string& pattern, const std::string& to) {
    str = std::regex_replace(str, std::regex(pattern), to);
}

// ============================================================================
// Benchmark 3: removeLayoutBinding — manual vs regex
// ============================================================================

std::string removeLayoutBinding_manual(const std::string& glslCode) {
    std::string result;
    result.reserve(glslCode.size());
    const size_t n = glslCode.size();
    size_t i = 0;

    while (i < n) {
        if (glslCode.compare(i, 7, "layout(") == 0) {
            size_t j = i + 7;
            int depth = 1;
            while (j < n && depth > 0) {
                if (glslCode[j] == '(') depth++;
                else if (glslCode[j] == ')') depth--;
                j++;
            }
            const char* inner = glslCode.data() + i + 7;
            size_t inner_len = j - i - 8;

            size_t bind_pos = std::string_view(inner, inner_len).find("binding");
            bool has_binding = (bind_pos != std::string_view::npos);

            if (has_binding) {
                std::string cleaned;
                cleaned.reserve(inner_len);
                size_t k = 0;
                while (k < inner_len) {
                    while (k < inner_len && (inner[k] == ' ' || inner[k] == '\t')) k++;
                    if (k >= inner_len) break;
                    size_t tok_start = k;
                    while (k < inner_len && inner[k] != ' ' && inner[k] != '\t' && inner[k] != ',' && inner[k] != '=') k++;
                    std::string_view tok(inner + tok_start, k - tok_start);
                    if (tok == "binding") {
                        while (k < inner_len && (inner[k] == ' ' || inner[k] == '\t')) k++;
                        if (k < inner_len && inner[k] == '=') {
                            k++;
                            while (k < inner_len && (inner[k] == ' ' || inner[k] == '\t')) k++;
                            while (k < inner_len && inner[k] >= '0' && inner[k] <= '9') k++;
                        }
                    } else {
                        if (!cleaned.empty()) cleaned += ' ';
                        cleaned.append(tok);
                    }
                    while (k < inner_len && (inner[k] == ' ' || inner[k] == '\t')) k++;
                    if (k < inner_len && inner[k] == ',') {
                        if (!cleaned.empty()) cleaned += ", ";
                        k++;
                    }
                }
                if (!cleaned.empty()) result += "layout(" + cleaned + ")";
                while (j < n && (glslCode[j] == ' ' || glslCode[j] == '\t' || glslCode[j] == '\n')) j++;
            } else {
                result.append(glslCode, i, j - i);
            }
            i = j;
        } else {
            result += glslCode[i++];
        }
    }
    return result;
}

std::string removeLayoutBinding_regex(const std::string& glslCode) {
    // Remove "binding = N" inside layout(...) — regex approach
    static const std::regex re(R"(binding\s*=\s*\d+\s*,?\s*)");
    std::string result = std::regex_replace(glslCode, re, "");
    // Clean up trailing commas: "layout(, " → "layout("
    static const std::regex clean(R"(layout\(\s*,\s*)");
    result = std::regex_replace(result, clean, "layout(");
    // Clean up ", )" → ")"
    static const std::regex clean2(R"(,\s*\))");
    result = std::regex_replace(result, clean2, ")");
    // Clean up empty layout: "layout()" → remove
    static const std::regex empty(R"(layout\(\s*\)\s*)");
    result = std::regex_replace(result, empty, "");
    return result;
}

// ============================================================================
// Benchmark 4: processOutColorLocations — manual vs regex
// ============================================================================

std::string processOutColorLocations_manual(const std::string& glslCode) {
    static const char* prefix = "\noutColor";
    std::string result;
    result.reserve(glslCode.size() + 128);
    size_t pos = 0;
    const size_t n = glslCode.size();

    while (pos < n) {
        size_t found = glslCode.find(prefix, pos);
        if (found == std::string::npos) {
            result.append(glslCode, pos, n - pos);
            break;
        }
        result.append(glslCode, pos, found - pos + 1);
        size_t num_start = found + 8;
        size_t num_end = num_start;
        while (num_end < n && glslCode[num_end] >= '0' && glslCode[num_end] <= '9') num_end++;
        if (num_end > num_start && num_end < n && glslCode[num_end] == ';') {
            std::string_view num(glslCode.data() + num_start, num_end - num_start);
            result += "layout(location=";
            result.append(num);
            result += ") outColor";
            result.append(num);
            result += ';';
            pos = num_end + 1;
        } else {
            result.append(glslCode, found + 1, 8);
            pos = found + 8;
        }
    }
    return result;
}

std::string processOutColorLocations_regex(const std::string& glslCode) {
    static const std::regex re(R"(\noutColor(\d+);)");
    return std::regex_replace(glslCode, re, "\nlayout(location=$1) outColor$1;");
}

// ============================================================================
// Benchmark 5: replace_line_starting_with — manual vs regex
// ============================================================================

std::string replace_line_starting_with_manual(const std::string& src, const std::string& prefix,
                                              const std::string& substitution = "") {
    std::string result;
    result.reserve(src.size());
    size_t len = src.size();
    size_t start = 0, cur = 0;

    while (cur < len) {
        size_t line_start = cur;
        while (cur < len && (src[cur] == ' ' || src[cur] == '\t')) cur++;
        bool match = (cur + prefix.size() <= len && src.compare(cur, prefix.size(), prefix) == 0);
        while (cur < len && src[cur] != '\r' && src[cur] != '\n') cur++;
        size_t nl_len = 0;
        if (cur < len) {
            nl_len = (src[cur] == '\r' && cur + 1 < len && src[cur + 1] == '\n') ? 2 : 1;
        }
        if (match) {
            if (line_start > start) result.append(src, start, line_start - start);
            cur += nl_len;
            start = cur;
            result += substitution;
        } else {
            cur += nl_len;
        }
    }
    if (cur > start) result.append(src, start, cur - start);
    return result;
}

std::string replace_line_starting_with_regex(const std::string& src, const std::string& prefix,
                                             const std::string& substitution = "") {
    std::string pattern = R"(^\s*)" + prefix + R"(.*(?:\r?\n)?)";
    static const std::regex re(pattern);
    return std::regex_replace(src, re, substitution);
}

// ============================================================================
// Main benchmark runner
// ============================================================================

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  GLSL Parser Benchmark: Manual vs std::regex" << std::endl;
    std::cout << "============================================================" << std::endl;

    const int repeat = 100;
    std::string large_shader = generate_large_shader(repeat);
    std::cout << "\nTest shader size: " << large_shader.size() << " bytes ("
              << repeat << "x repetition)" << std::endl;
    std::cout << "Each test runs 1000 iterations.\n" << std::endl;

    const int iterations = 1000;
    volatile int dummy = 0; // prevent optimization

    // --- Benchmark 1: getGLSLVersion ---
    std::cout << "--- Benchmark 1: getGLSLVersion ---" << std::endl;
    {
        Timer t("Manual version scan");
        for (int i = 0; i < iterations; i++) dummy = getGLSLVersion_manual(large_shader.c_str());
    }
    {
        Timer t("Regex version scan");
        for (int i = 0; i < iterations; i++) dummy = getGLSLVersion_regex(large_shader);
    }
    std::cout << std::endl;

    // --- Benchmark 2: replace_all ---
    std::cout << "--- Benchmark 2: replace_all (\"texture2D\" → \"texture\") ---" << std::endl;
    {
        std::string copy = large_shader;
        Timer t("Manual replace_all");
        for (int i = 0; i < iterations; i++) {
            std::string s = copy;
            replace_all_manual(s, "texture2D", "texture");
            dummy += s.size();
        }
    }
    {
        std::string copy = large_shader;
        Timer t("Regex replace_all");
        for (int i = 0; i < iterations; i++) {
            std::string s = copy;
            replace_all_regex(s, "texture2D", "texture");
            dummy += s.size();
        }
    }
    std::cout << std::endl;

    // --- Benchmark 3: removeLayoutBinding ---
    std::cout << "--- Benchmark 3: removeLayoutBinding ---" << std::endl;
    {
        Timer t("Manual removeLayoutBinding");
        for (int i = 0; i < iterations; i++) {
            std::string s = removeLayoutBinding_manual(large_shader);
            dummy += s.size();
        }
    }
    {
        Timer t("Regex removeLayoutBinding");
        for (int i = 0; i < iterations; i++) {
            std::string s = removeLayoutBinding_regex(large_shader);
            dummy += s.size();
        }
    }
    std::cout << std::endl;

    // --- Benchmark 4: processOutColorLocations ---
    std::cout << "--- Benchmark 4: processOutColorLocations ---" << std::endl;
    {
        Timer t("Manual outColor locations");
        for (int i = 0; i < iterations; i++) {
            std::string s = processOutColorLocations_manual(large_shader);
            dummy += s.size();
        }
    }
    {
        Timer t("Regex outColor locations");
        for (int i = 0; i < iterations; i++) {
            std::string s = processOutColorLocations_regex(large_shader);
            dummy += s.size();
        }
    }
    std::cout << std::endl;

    // --- Benchmark 5: replace_line_starting_with ---
    std::cout << "--- Benchmark 5: replace_line_starting_with (\"#line\") ---" << std::endl;
    {
        Timer t("Manual line replace");
        for (int i = 0; i < iterations; i++) {
            std::string s = replace_line_starting_with_manual(large_shader, "#line", "// removed\n");
            dummy += s.size();
        }
    }
    {
        Timer t("Regex line replace");
        for (int i = 0; i < iterations; i++) {
            std::string s = replace_line_starting_with_regex(large_shader, "#line", "// removed\n");
            dummy += s.size();
        }
    }
    std::cout << std::endl;

    std::cout << "============================================================" << std::endl;
    std::cout << "  Benchmark complete. (dummy=" << dummy << ")" << std::endl;
    std::cout << "============================================================" << std::endl;

    return 0;
}