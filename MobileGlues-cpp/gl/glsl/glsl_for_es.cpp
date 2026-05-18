// MobileGlues - gl/glsl/glsl_for_es.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
#include "glsl_for_es.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Include/Types.h>
#include <glslang/Public/ShaderLang.h>
#include <spirv_cross/spirv_cross_c.h>
#include <iostream>
#include <fstream>
#include "../log.h"
#include "glslang/SPIRV/GlslangToSpv.h"
#include <string>
#include <regex>
#include <strstream>
#include <algorithm>
#include <sstream>
#include <cstring>
#include "cache.h"
#include "../../version.h"

#define DEBUG 0

const char* atomicCounterEmulatedWatermark = "// Non-opaque atomic uniform converted to SSBO";

static TBuiltInResource InitResources() {
    TBuiltInResource Resources{};

    Resources.maxLights = 32;
    Resources.maxClipPlanes = 6;
    Resources.maxTextureUnits = 32;
    Resources.maxTextureCoords = 32;
    Resources.maxVertexAttribs = 64;
    Resources.maxVertexUniformComponents = 4096;
    Resources.maxVaryingFloats = 64;
    Resources.maxVertexTextureImageUnits = 32;
    Resources.maxCombinedTextureImageUnits = 80;
    Resources.maxTextureImageUnits = 32;
    Resources.maxFragmentUniformComponents = 4096;
    Resources.maxDrawBuffers = 32;
    Resources.maxVertexUniformVectors = 128;
    Resources.maxVaryingVectors = 8;
    Resources.maxFragmentUniformVectors = 16;
    Resources.maxVertexOutputVectors = 16;
    Resources.maxFragmentInputVectors = 15;
    Resources.minProgramTexelOffset = -8;
    Resources.maxProgramTexelOffset = 7;
    Resources.maxClipDistances = 8;
    Resources.maxComputeWorkGroupCountX = 65535;
    Resources.maxComputeWorkGroupCountY = 65535;
    Resources.maxComputeWorkGroupCountZ = 65535;
    Resources.maxComputeWorkGroupSizeX = 1024;
    Resources.maxComputeWorkGroupSizeY = 1024;
    Resources.maxComputeWorkGroupSizeZ = 64;
    Resources.maxComputeUniformComponents = 1024;
    Resources.maxComputeTextureImageUnits = 16;
    Resources.maxComputeImageUniforms = 8;
    Resources.maxComputeAtomicCounters = 8;
    Resources.maxComputeAtomicCounterBuffers = 1;
    Resources.maxVaryingComponents = 60;
    Resources.maxVertexOutputComponents = 64;
    Resources.maxGeometryInputComponents = 64;
    Resources.maxGeometryOutputComponents = 128;
    Resources.maxFragmentInputComponents = 128;
    Resources.maxImageUnits = 8;
    Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
    Resources.maxCombinedShaderOutputResources = 8;
    Resources.maxImageSamples = 0;
    Resources.maxVertexImageUniforms = 0;
    Resources.maxTessControlImageUniforms = 0;
    Resources.maxTessEvaluationImageUniforms = 0;
    Resources.maxGeometryImageUniforms = 0;
    Resources.maxFragmentImageUniforms = 8;
    Resources.maxCombinedImageUniforms = 8;
    Resources.maxGeometryTextureImageUnits = 16;
    Resources.maxGeometryOutputVertices = 256;
    Resources.maxGeometryTotalOutputComponents = 1024;
    Resources.maxGeometryUniformComponents = 1024;
    Resources.maxGeometryVaryingComponents = 64;
    Resources.maxTessControlInputComponents = 128;
    Resources.maxTessControlOutputComponents = 128;
    Resources.maxTessControlTextureImageUnits = 16;
    Resources.maxTessControlUniformComponents = 1024;
    Resources.maxTessControlTotalOutputComponents = 4096;
    Resources.maxTessEvaluationInputComponents = 128;
    Resources.maxTessEvaluationOutputComponents = 128;
    Resources.maxTessEvaluationTextureImageUnits = 16;
    Resources.maxTessEvaluationUniformComponents = 1024;
    Resources.maxTessPatchComponents = 120;
    Resources.maxPatchVertices = 32;
    Resources.maxTessGenLevel = 64;
    Resources.maxViewports = 16;
    Resources.maxVertexAtomicCounters = 0;
    Resources.maxTessControlAtomicCounters = 0;
    Resources.maxTessEvaluationAtomicCounters = 0;
    Resources.maxGeometryAtomicCounters = 0;
    Resources.maxFragmentAtomicCounters = 8;
    Resources.maxCombinedAtomicCounters = 8;
    Resources.maxAtomicCounterBindings = 1;
    Resources.maxVertexAtomicCounterBuffers = 0;
    Resources.maxTessControlAtomicCounterBuffers = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers = 0;
    Resources.maxGeometryAtomicCounterBuffers = 0;
    Resources.maxFragmentAtomicCounterBuffers = 1;
    Resources.maxCombinedAtomicCounterBuffers = 1;
    Resources.maxAtomicCounterBufferSize = 16384;
    Resources.maxTransformFeedbackBuffers = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances = 8;
    Resources.maxCombinedClipAndCullDistances = 8;
    Resources.maxSamples = 4;
    Resources.maxMeshOutputVerticesNV = 256;
    Resources.maxMeshOutputPrimitivesNV = 512;
    Resources.maxMeshWorkGroupSizeX_NV = 32;
    Resources.maxMeshWorkGroupSizeY_NV = 1;
    Resources.maxMeshWorkGroupSizeZ_NV = 1;
    Resources.maxTaskWorkGroupSizeX_NV = 32;
    Resources.maxTaskWorkGroupSizeY_NV = 1;
    Resources.maxTaskWorkGroupSizeZ_NV = 1;
    Resources.maxMeshViewCountNV = 4;

    Resources.limits.nonInductiveForLoops = true;
    Resources.limits.whileLoops = true;
    Resources.limits.doWhileLoops = true;
    Resources.limits.generalUniformIndexing = true;
    Resources.limits.generalAttributeMatrixVectorIndexing = true;
    Resources.limits.generalVaryingIndexing = true;
    Resources.limits.generalSamplerIndexing = true;
    Resources.limits.generalVariableIndexing = true;
    Resources.limits.generalConstantMatrixVectorIndexing = true;

    return Resources;
}

int getGLSLVersion(const char* glsl_code) {
    if (strncmp(glsl_code, "#version ", 9) == 0) {
        return atoi(glsl_code + 9);
    }
    return -1;
}

std::string forceSupporterOutput(const std::string& glslCode) {
    bool hasPrecisionFloat =
        glslCode.find("precision ") != std::string::npos && glslCode.find("float;") != std::string::npos;
    bool hasPrecisionInt =
        glslCode.find("precision ") != std::string::npos && glslCode.find("int;") != std::string::npos;

    std::string result = glslCode;
    std::string precisionFloat;
    std::string precisionInt;

    if (hasPrecisionFloat && hasPrecisionInt) {
        std::istringstream iss(result);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(iss, line)) {
            bool isPrecisionLine = (line.find("precision ") != std::string::npos) &&
                                   (line.find("float;") != std::string::npos || line.find("int;") != std::string::npos);
            if (!isPrecisionLine) {
                lines.push_back(line);
            }
        }
        result.clear();
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i != 0) result += '\n';
            result += lines[i];
        }
        precisionFloat = "precision highp float;\n";
        precisionInt = "precision highp int;\n";
    } else {
        precisionFloat = hasPrecisionFloat ? "" : "precision highp float;\n";
        precisionInt = hasPrecisionInt ? "" : "precision highp int;\n";
    }

    size_t lastExtensionPos = result.rfind("#extension");
    size_t insertionPos = 0;

    if (lastExtensionPos != std::string::npos) {
        size_t nextNewline = result.find('\n', lastExtensionPos);
        if (nextNewline != std::string::npos) {
            insertionPos = nextNewline + 1;
        } else {
            insertionPos = result.length();
        }
    } else {
        size_t firstNewline = result.find('\n');
        if (firstNewline != std::string::npos) {
            insertionPos = firstNewline + 1;
        } else {
            result = precisionFloat + precisionInt + result;
            return result;
        }
    }

    result.insert(insertionPos, precisionFloat + precisionInt);
    return result;
}

std::string removeLayoutBinding(const std::string& glslCode) {
    std::string result;
    result.reserve(glslCode.size());
    const size_t len = glslCode.size();
    size_t pos = 0;

    while (pos < len) {
        size_t lp = glslCode.find("layout", pos);
        if (lp == std::string::npos) {
            result.append(glslCode, pos, len - pos);
            break;
        }
        result.append(glslCode, pos, lp - pos);

        size_t op = lp + 6;
        while (op < len && isspace(glslCode[op])) op++;
        if (op >= len || glslCode[op] != '(') {
            result += "layout";
            pos = lp + 6;
            continue;
        }

        size_t cp = glslCode.find(')', op);
        if (cp == std::string::npos) {
            result.append(glslCode, lp, len - lp);
            break;
        }

        std::string_view inner(glslCode.data() + op + 1, cp - op - 1);

        size_t bpos = inner.find("binding");
        if (bpos == std::string::npos) {
            result.append(glslCode, lp, cp + 1 - lp);
            pos = cp + 1;
            continue;
        }

        bool hasOther = false;
        {
            size_t s = 0;
            while (s < inner.size()) {
                while (s < inner.size() && isspace(inner[s])) s++;
                if (s >= inner.size()) break;
                size_t start = s;
                while (s < inner.size() && inner[s] != ',') s++;
                std::string_view item(inner.data() + start, s - start);
                while (!item.empty() && isspace(item.front())) item.remove_prefix(1);
                while (!item.empty() && isspace(item.back())) item.remove_suffix(1);
                if (item.size() < 7 || item.substr(0, 7) != "binding") {
                    hasOther = true;
                    break;
                }
                if (s < inner.size()) s++;
            }
        }

        if (!hasOther) {
            pos = cp + 1;
            while (pos < len && isspace(glslCode[pos])) pos++;
        } else {
            std::string cleaned;
            size_t s = 0;
            bool first = true;
            while (s < inner.size()) {
                while (s < inner.size() && isspace(inner[s])) s++;
                if (s >= inner.size()) break;
                size_t start = s;
                while (s < inner.size() && inner[s] != ',') s++;
                std::string_view item(inner.data() + start, s - start);
                while (!item.empty() && isspace(item.front())) item.remove_prefix(1);
                while (!item.empty() && isspace(item.back())) item.remove_suffix(1);
                if (item.size() < 7 || item.substr(0, 7) != "binding") {
                    if (!first) cleaned += ", ";
                    cleaned.append(item.data(), item.size());
                    first = false;
                }
                if (s < inner.size()) s++;
            }
            result += "layout(" + cleaned + ")";
            pos = cp + 1;
            while (pos < len && isspace(glslCode[pos])) pos++;
        }
    }
    return result;
}

void trim(std::string& str) {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](int ch) { return !std::isspace(ch); }));
    str.erase(std::find_if(str.rbegin(), str.rend(), [](int ch) { return !std::isspace(ch); }).base(), str.end());
}

// Process all uniform declarations into `uniform <precision> <type> <name>;` form
std::string process_uniform_declarations(const std::string& glslCode) {
    std::string result;
    size_t scan_pos = 0;
    size_t chunk_start = 0;
    const size_t length = glslCode.length();
    const std::vector<std::string> precision_kws = {"highp", "lowp", "mediump"};

    result.reserve(glslCode.length());

    while (scan_pos < length) {
        if (glslCode.compare(scan_pos, 7, "uniform") == 0) {
            if (scan_pos > chunk_start) {
                result.append(glslCode, chunk_start, scan_pos - chunk_start);
            }

            const size_t decl_start = scan_pos;
            scan_pos += 7; // Skip "uniform"

            std::string precision, type;
            bool found_precision = false;

            while (scan_pos < length) {
                while (scan_pos < length && std::isspace(glslCode[scan_pos]))
                    ++scan_pos;

                for (const auto& kw : precision_kws) {
                    if (glslCode.compare(scan_pos, kw.length(), kw) == 0) {
                        precision = " " + kw;
                        scan_pos += kw.length();
                        found_precision = true;
                        break;
                    }
                }
                if (found_precision) break;

                const size_t type_start = scan_pos;
                while (scan_pos < length && (std::isalnum(glslCode[scan_pos]) || glslCode[scan_pos] == '_')) {
                    ++scan_pos;
                }
                type = glslCode.substr(type_start, scan_pos - type_start);
                break;
            }

            while (scan_pos < length) {
                while (scan_pos < length && std::isspace(glslCode[scan_pos]))
                    ++scan_pos;

                bool found = false;
                for (const auto& kw : precision_kws) {
                    if (glslCode.compare(scan_pos, kw.length(), kw) == 0) {
                        if (precision.empty()) precision = " " + kw;
                        scan_pos += kw.length();
                        found = true;
                        break;
                    }
                }
                if (!found) break;
            }

            if (type.empty()) {
                const size_t type_start = scan_pos;
                while (scan_pos < length && (std::isalnum(glslCode[scan_pos]) || glslCode[scan_pos] == '_')) {
                    ++scan_pos;
                }
                type = glslCode.substr(type_start, scan_pos - type_start);
            }

            while (scan_pos < length && std::isspace(glslCode[scan_pos]))
                ++scan_pos;
            const size_t name_start = scan_pos;
            while (scan_pos < length && (std::isalnum(glslCode[scan_pos]) || glslCode[scan_pos] == '_')) {
                ++scan_pos;
            }
            const std::string name = glslCode.substr(name_start, scan_pos - name_start);

            size_t decl_end = glslCode.find(';', scan_pos);
            if (decl_end == std::string::npos)
                decl_end = length;
            else
                ++decl_end;
            const bool has_initializer = (glslCode.find('=', scan_pos) < decl_end);
            if (has_initializer) {
                result.append("uniform").append(precision).append(" ").append(type).append(" ").append(name).append(
                    ";");
            } else {
                result.append(glslCode, decl_start, decl_end - decl_start);
            }

            scan_pos = chunk_start = decl_end;
        } else {
            ++scan_pos;
        }
    }

    if (chunk_start < length) {
        result.append(glslCode, chunk_start, length - chunk_start);
    }

    return result;
}

std::string processOutColorLocations(const std::string& glslCode) {
    std::string result;
    result.reserve(glslCode.size() + 64);
    const size_t len = glslCode.size();
    size_t pos = 0;
    const std::string needle = " outColor";

    while (pos < len) {
        size_t oc = glslCode.find(needle, pos);
        if (oc == std::string::npos) {
            result.append(glslCode, pos, len - pos);
            break;
        }

        size_t numStart = oc + needle.size();
        if (numStart >= len || !isdigit(glslCode[numStart])) {
            result.append(glslCode, pos, oc + needle.size() - pos);
            pos = oc + needle.size();
            continue;
        }

        std::string num;
        while (numStart < len && isdigit(glslCode[numStart])) {
            num += glslCode[numStart++];
        }

        if (!num.empty() && numStart < len && glslCode[numStart] == ';') {
            result.append(glslCode, pos, oc - pos);
            result += "\nlayout(location=" + num + ") ";
            result.append(glslCode, oc + 1, needle.size() - 1);
            pos = oc + 1;
        } else {
            result.append(glslCode, pos, oc + needle.size() - pos);
            pos = oc + needle.size();
        }
    }
    return result;
}

bool checkIfAtomicCounterBufferEmulated(const char* glslCode) {
    return strstr(glslCode, atomicCounterEmulatedWatermark) != nullptr;
}

std::string GLSLtoGLSLES(const char* glsl_code, GLenum glsl_type, uint essl_version, uint glsl_version,
                         int& return_code) {
    std::string sha256_string(glsl_code);
    sha256_string += "\n//" + std::to_string(MAJOR) + "." + std::to_string(MINOR) + "." + std::to_string(REVISION) +
                     "|" + std::to_string(essl_version);
    const char* cachedESSL = Cache::get_instance().get(sha256_string.c_str());
    if (cachedESSL) {
        LOG_D("GLSL Hit Cache:\n%s\n-->\n%s", glsl_code, cachedESSL)
        bool atomicCounterEmulated = checkIfAtomicCounterBufferEmulated(cachedESSL);
        return_code = atomicCounterEmulated ? 1 : 0;
        return (char*)cachedESSL;
    }

    return_code = -1;
    // std::string converted = glsl_version<140? GLSLtoGLSLES_1(glsl_code, glsl_type, essl_version,
    // return_code):GLSLtoGLSLES_2(glsl_code, glsl_type, essl_version, return_code);
    std::string converted = GLSLtoGLSLES_2(glsl_code, glsl_type, essl_version, return_code);
    if (return_code >= 0 && !converted.empty()) {
        converted = process_uniform_declarations(converted);
        Cache::get_instance().put(sha256_string.c_str(), converted.c_str());
    }

    return (return_code >= 0) ? converted : glsl_code;
}

std::string replace_line_starting_with(const std::string& glslCode, const std::string& starting,
                                       const std::string& substitution = "") {
    std::string result;
    size_t length = glslCode.size();
    size_t start = 0;
    size_t current = 0;

    auto append_chunk = [&](size_t end) {
        if (end > start) {
            result.append(glslCode, start, end - start);
        }
    };

    while (current < length) {
        // Skip whitespace at line begin
        size_t lineStart = current;
        while (current < length && (glslCode[current] == ' ' || glslCode[current] == '\t')) {
            current++;
        }

        // Check whether #line directive
        bool isLineDirective = false;
        if (current + 5 <= length && glslCode.compare(current, 5, "#line") == 0) {
            isLineDirective = true;
        }

        // Move to line end
        while (current < length && glslCode[current] != '\r' && glslCode[current] != '\n') {
            current++;
        }

        // Handle carriage return
        size_t newlineLength = 0;
        if (current < length) {
            if (glslCode[current] == '\r') {
                newlineLength = (current + 1 < length && glslCode[current + 1] == '\n') ? 2 : 1;
            } else {
                newlineLength = 1;
            }
        }

        if (isLineDirective) {
            // Find #line directive ->
            //  1. Append chunk
            append_chunk(lineStart); // from chunk_begin to before `#line`
            // 2. Skip this line (incl. \n)
            current += newlineLength;
            start = current; // 3. Starting from next line

            result += substitution;
        } else {
            // move to a new line
            current += newlineLength;
        }
    }

    // append last block
    append_chunk(current);
    return result;
}

static inline void replace_all(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
}

static size_t find_insertion_point(const std::string& glsl) {
    size_t pos = 0;
    size_t insertion_point = 0;

    size_t version_pos = glsl.find("#version");
    if (version_pos != std::string::npos) {
        size_t version_end = glsl.find('\n', version_pos);
        if (version_end == std::string::npos) {
            version_end = glsl.length();
        } else {
            version_end++;
        }
        insertion_point = version_end;
        pos = version_end;
    } else {
        insertion_point = 0;
        pos = 0;
    }

    while (pos < glsl.length()) {
        size_t line_begin = pos;
        while (pos < glsl.length() && std::isspace(glsl[pos])) {
            pos++;
        }
        if (pos >= glsl.length()) break;

        if (glsl[pos] == '#') {
            pos++;
            while (pos < glsl.length() && std::isspace(glsl[pos])) {
                pos++;
            }
            if (glsl.compare(pos, 9, "extension") == 0) {
                size_t ext_end = glsl.find('\n', pos);
                if (ext_end == std::string::npos) {
                    ext_end = glsl.length();
                } else {
                    ext_end++;
                }
                insertion_point = ext_end;
                pos = ext_end;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return insertion_point;
}

static std::string replaceAll(const std::string& source, const std::string& from, const std::string& to) {
    if (from.empty()) return source;
    std::string result;
    result.reserve(source.size());
    size_t pos = 0;
    size_t found;
    while ((found = source.find(from, pos)) != std::string::npos) {
        result.append(source, pos, found - pos);
        result.append(to);
        pos = found + from.size();
    }
    result.append(source, pos, source.size() - pos);
    return result;
}

bool process_non_opaque_atomic_to_ssbo(std::string& source) {
    if (source.find("atomicCounter") == std::string::npos) return false;

    std::set<std::string> atomic_vars;
    std::map<std::string, std::string> binding_map;
    std::regex decl_rx(
        R"(layout\s*\(\s*binding\s*=\s*(\d+)\s*(?:,\s*offset\s*=\s*(\d+)\s*)?\)\s*uniform\s+atomic_uint\s+(\w+)\s*;)",
        std::regex::icase);

    std::smatch m;
    auto it = source.cbegin();
    while (std::regex_search(it, source.cend(), m, decl_rx)) {
        size_t prefix = std::distance(source.cbegin(), it);
        size_t match_pos = prefix + m.position(0);
        size_t match_len = m.length(0);

        std::string binding = m[1].str();
        std::string var = m[3].str();
        atomic_vars.insert(var);
        binding_map[var] = binding;

        std::string repl = "layout(std430, binding=" + binding + ") buffer AtomicCounterSSBO_" + binding +
                           " {\n"
                           "    uint " +
                           var +
                           ";\n"
                           "};\n";
        source.replace(match_pos, match_len, repl);

        it = source.cbegin() + match_pos + repl.size();
    }

    if (atomic_vars.empty()) return true;

    for (auto& var : atomic_vars) {
        source = replaceAll(source,
            "atomicCounterIncrement(" + var + ")",
            "atomicAdd(" + var + ", 1u)");
        source = replaceAll(source,
            "atomicCounterDecrement(" + var + ")",
            "atomicAdd(" + var + ", uint(-1))");
        source = replaceAll(source,
            "atomicCounterAdd(" + var + ",",
            "atomicAdd(" + var + ",");
        size_t pos = 0;
        const std::string prefix = "atomicCounter(" + var + ")";
        while ((pos = source.find(prefix, pos)) != std::string::npos) {
            source.replace(pos, prefix.size(), var);
            pos += var.size();
        }
    }

    {
        size_t pos = 0;
        size_t len = source.size();
        bool prevWasAtomicAdd = false;
        while (pos < len) {
            size_t found = source.find("atomicAdd", pos);
            if (found == std::string::npos) break;
            
            size_t stmtEnd = source.find(';', found);
            if (stmtEnd == std::string::npos) break;
            
            if (!prevWasAtomicAdd) {
                source.insert(stmtEnd + 1, "\n    memoryBarrierBuffer();");
                len = source.size();
                prevWasAtomicAdd = true;
            }
            pos = stmtEnd + 1;
        }
    }

    source += "\n" + std::string(atomicCounterEmulatedWatermark);
    return true;
}

void process_sampler_buffer(std::string& source) { // a simplized version, should be rewritten in the future
    if (source.find("isamplerBuffer") == std::string::npos) {
        return;
    }

    size_t pos = 0;
    while ((pos = source.find("isamplerBuffer", pos)) != std::string::npos) {
        source.replace(pos, 14, "isampler2D");
        pos += 11;
    }

    size_t tf2 = 0;
    while ((tf2 = source.find("texelFetch(", tf2)) != std::string::npos) {
        size_t tfn_start = tf2 + 11;
        size_t comma = source.find(',', tfn_start);
        if (comma == std::string::npos) break;
        size_t close = source.find(')', comma + 1);
        if (close == std::string::npos) break;
        if (source.find("ivec2", tfn_start) != std::string::npos &&
            source.find("ivec2", tfn_start) < (comma == std::string::npos ? std::string::npos : comma)) {
            tf2 = close + 1;
            continue;
        }
        std::string name = source.substr(tfn_start, comma - tfn_start);
        std::string index = source.substr(comma + 1, close - comma - 1);
        while (!name.empty() && isspace(name.front())) name.erase(0, 1);
        while (!name.empty() && isspace(name.back())) name.pop_back();
        while (!index.empty() && isspace(index.front())) index.erase(0, 1);
        while (!index.empty() && isspace(index.back())) index.pop_back();
        std::string before = source.substr(0, tf2);
        std::string after = source.substr(close + 1);
        source = before + "texelFetch(" + name + ", ivec2((" + index
               + ") % u_BufferTexWidth, (" + index + ") / u_BufferTexWidth), 0)" + after;
        tf2 = before.size() + 12;
    }

    const char* boundaryProtection = R"(
ivec2 bufferCoords(int index) {
    int width = u_BufferTexWidth;
    int x = index % width;
    int y = index / width;
    if (y >= u_BufferTexHeight) {
        y = u_BufferTexHeight - 1;
        x = width - 1;
    }
    return ivec2(x, y);
}
)";

    size_t tf_pos = 0;
    while ((tf_pos = source.find("texelFetch(", tf_pos)) != std::string::npos) {
        size_t arg1_end = source.find(',', tf_pos);
        if (arg1_end == std::string::npos) break;
        size_t arg2_start = source.find("ivec2(", arg1_end);
        if (arg2_start == std::string::npos) break;
        size_t arg2_end = source.find(")", arg2_start);
        if (arg2_end == std::string::npos) break;
        size_t close_paren = source.find(", 0)", arg2_end);
        if (close_paren == std::string::npos || close_paren > arg2_end + 20) {
            tf_pos = arg1_end + 1;
            continue;
        }
        std::string before = source.substr(0, tf_pos);
        std::string tex_name = source.substr(tf_pos + 11, arg1_end - tf_pos - 11);
        std::string ivec2_expr = source.substr(arg2_start + 6, arg2_end - arg2_start - 6);
        std::string after = source.substr(close_paren + 4);
        source = before + "texelFetch(" + tex_name + ", bufferCoords(" + ivec2_expr + "), 0)" + after;
        tf_pos = before.size() + 10;
    }

    size_t insertion_point = find_insertion_point(source);
    if (insertion_point != std::string::npos) {
        source.insert(insertion_point, boundaryProtection);
    }

    const char* uniformDecl = R"(
uniform int u_BufferTexWidth;
uniform int u_BufferTexHeight;
)";

    insertion_point = find_insertion_point(source);
    if (insertion_point != std::string::npos) {
        insertion_point = source.find('\n', insertion_point);
        if (insertion_point != std::string::npos) {
            source.insert(insertion_point + 1, uniformDecl);
        }
    }
}

static void inject_textureQueryLod(std::string& glsl) {
    if (glsl.find("textureQueryLod") == std::string::npos) {
        return;
    }
    if (glsl.find("vec2 mg_textureQueryLod(") != std::string::npos) {
        return;
    }

    const std::string textureQueryLodImpl = R"(
#define textureQueryLod mg_textureQueryLod

vec2 mg_textureQueryLod(sampler2D tex, vec2 uv) {
    vec2 texSizeF = vec2(textureSize(tex, 0));
    vec2 dFdx_uv = dFdx(uv * texSizeF);
    vec2 dFdy_uv = dFdy(uv * texSizeF);
    float maxDerivative = max(length(dFdx_uv), length(dFdy_uv));
    float lod = log2(maxDerivative);
    return vec2(lod);
}
)";

    size_t insertPos = find_insertion_point(glsl);
    glsl.insert(insertPos, "\n" + textureQueryLodImpl + "\n");
}

static inline void inject_temporal_filter(std::string& glsl) {
    if (glsl.find("GI_TemporalFilter") == std::string::npos) {
        return;
    }
    if (glsl.find("vec4 GI_TemporalFilter(") != std::string::npos) {
        return;
    }

    size_t insertPos = glsl.find("uniform");
    if (insertPos != std::string::npos) {
        insertPos = glsl.rfind('\n', insertPos);
        if (insertPos != std::string::npos && insertPos > 0) {
            insertPos++;
        } else {
            insertPos = glsl.find('\n', glsl.find("// main", 0));
            if (insertPos == std::string::npos) insertPos = 0;
            else insertPos++;
        }
    }

    const std::string GI_TemporalFilterImpl = R"(
vec4 GI_TemporalFilter() {
    vec2 uv = gl_FragCoord.xy / screenSize;
    uv += taaJitter * pixelSize;
    vec4 currentGI = texture(colortex0, uv);
    float depth = texture(depthtex0, uv).r;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = gbufferProjectionInverse * clipPos;
    viewPos /= viewPos.w;
    vec4 worldPos = gbufferModelViewInverse * viewPos;
    vec4 prevClipPos = gbufferPreviousProjection * (gbufferPreviousModelView * worldPos);
    prevClipPos /= prevClipPos.w;
    vec2 prevUV = prevClipPos.xy * 0.5 + 0.5;
    vec4 historyGI = texture(colortex1, prevUV);
    float difference = length(currentGI.rgb - historyGI.rgb);
    float thresholdValue = 0.1;
    float adaptiveBlend = mix(0.9, 0.0, smoothstep(thresholdValue, thresholdValue * 2.0, difference));
    vec4 filteredGI = mix(currentGI, historyGI, adaptiveBlend);
    if (difference > thresholdValue * 2.0) {
        filteredGI = currentGI;
    }
    return filteredGI;
}
)";
    glsl.insert(insertPos, "\n" + GI_TemporalFilterImpl + "\n");
}
#define xstr(s) str(s)
#define str(s) #s

void inject_mg_macro_definition(std::string& glslCode) {
    std::string macro_definitions =
        "\n#define MG_MOBILEGLUES\n"
        "#define MG_MOBILEGLUES_VERSION " xstr(MAJOR) xstr(MINOR) xstr(REVISION) xstr(PATCH) "\n";

    size_t versionPos = glslCode.rfind("#version");
    size_t insertionPos = 0;

    if (versionPos != std::string::npos) {
        size_t nextNewline = glslCode.find('\n', versionPos);
        insertionPos = (nextNewline != std::string::npos) ? nextNewline + 1 : glslCode.length();
    } else {
        size_t firstNewline = glslCode.find('\n');
        insertionPos = (firstNewline != std::string::npos) ? firstNewline + 1 : 0;
    }

    glslCode.insert(insertionPos, macro_definitions);
}

std::string preprocess_glsl(const std::string& glsl, GLenum shaderType, bool* atomicCounterEmulated) {
    std::string ret = glsl;
    // Remove lines beginning with `#line`
    ret = replace_line_starting_with(ret, "#line");
    // Act as if disable_GL_ARB_derivative_control is false
    replace_all(ret, "#ifdef GL_ARB_derivative_control", "#if 0");
    replace_all(ret, "#ifndef GL_ARB_derivative_control", "#if 1");

    // Polyfill transpose()
    replace_all(ret, "const mat3 rotInverse = transpose(rot);",
                "const mat3 rotInverse = mat3(rot[0][0], rot[1][0], rot[2][0], rot[0][1], rot[1][1], rot[2][1], "
                "rot[0][2], rot[1][2], rot[2][2]);");

    // GI_TemporalFilter injection
    inject_temporal_filter(ret);

    // textureQueryLod injection
    if (!g_gles_caps.GL_EXT_texture_query_lod) {
        inject_textureQueryLod(ret);
    }

    // MobileGlues macros injection
    inject_mg_macro_definition(ret);

    if (hardware->emulate_texture_buffer) {
        // Sampler buffer processing
        process_sampler_buffer(ret);
    }

    *atomicCounterEmulated = process_non_opaque_atomic_to_ssbo(ret);
    return ret;
}

int get_or_add_glsl_version(std::string& glsl) {
    int glsl_version = getGLSLVersion(glsl.c_str());
    if (glsl_version == -1) {
        glsl_version = 150;
        glsl.insert(0, "#version 150\n");
    } else if (glsl_version < 140) {
        // force upgrade glsl version
        glsl = replace_line_starting_with(glsl, "#version", "#version 150 compatibility\n");
        glsl_version = 150;
    }

    LOG_D("GLSL version: %d", glsl_version)
    return glsl_version;
}

std::vector<unsigned int> glsl_to_spirv(GLenum shader_type, int glsl_version, const char* const* shader_src,
                                        int& errc) {
    EShLanguage shader_language;
    switch (shader_type) {
    case GL_VERTEX_SHADER:
        shader_language = EShLanguage::EShLangVertex;
        break;
    case GL_FRAGMENT_SHADER:
        shader_language = EShLanguage::EShLangFragment;
        break;
    case GL_COMPUTE_SHADER:
        shader_language = EShLanguage::EShLangCompute;
        break;
    case GL_TESS_CONTROL_SHADER:
        shader_language = EShLanguage::EShLangTessControl;
        break;
    case GL_TESS_EVALUATION_SHADER:
        shader_language = EShLanguage::EShLangTessEvaluation;
        break;
    case GL_GEOMETRY_SHADER:
        shader_language = EShLanguage::EShLangGeometry;
        break;
    default:
        LOG_D("GLSL type not supported!")
        errc = -1;
        return {};
    }

    glslang::TShader shader(shader_language);
    shader.setStrings(shader_src, 1);

    using namespace glslang;
    shader.setEnvInput(EShSourceGlsl, shader_language, EShClientVulkan, glsl_version);
    shader.setEnvClient(EShClientOpenGL, EShTargetOpenGL_450);
    shader.setEnvTarget(EShTargetSpv, EShTargetSpv_1_5);
    shader.setAutoMapLocations(true);
    shader.setAutoMapBindings(true);

    TBuiltInResource TBuiltInResource_resources = InitResources();

    if (!shader.parse(&TBuiltInResource_resources, glsl_version, true, EShMsgDefault)) {
        LOG_D("GLSL Compiling ERROR: \n%s", shader.getInfoLog())
        errc = -1;
        return {};
    }
    LOG_D("GLSL Compiled.")

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(EShMsgDefault)) {
        LOG_D("Shader Linking ERROR: %s", program.getInfoLog())
        errc = -1;
        return {};
    }
    LOG_D("Shader Linked.")
    std::vector<unsigned int> spirv_code;
    glslang::SpvOptions spvOptions;
    spvOptions.disableOptimizer = false;
    glslang::GlslangToSpv(*program.getIntermediate(shader_language), spirv_code, &spvOptions);
    errc = 0;
    return spirv_code;
}

std::string spirv_to_essl(std::vector<unsigned int> spirv, uint essl_version, int& errc) {
    spvc_context context = nullptr;
    spvc_parsed_ir ir = nullptr;
    spvc_compiler compiler_glsl = nullptr;
    spvc_compiler_options options = nullptr;
    spvc_resources resources = nullptr;
    const char* result = nullptr;
    size_t count;

    const SpvId* p_spirv = spirv.data();
    size_t word_count = spirv.size();

    LOG_D("spirv_code.size(): %d", spirv.size())
    spvc_context_create(&context);
    spvc_context_parse_spirv(context, p_spirv, word_count, &ir);
    spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler_glsl);
    spvc_compiler_create_shader_resources(compiler_glsl, &resources);
    spvc_compiler_create_compiler_options(compiler_glsl, &options);
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_GLSL_VERSION,
                                   essl_version >= 300 ? essl_version : 300);
    spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES, SPVC_TRUE);
    spvc_compiler_install_compiler_options(compiler_glsl, options);
    spvc_compiler_compile(compiler_glsl, &result);

    if (!result) {
        LOG_E("Error: unexpected error in spirv-cross.")
        errc = -1;
        return "";
    }

    std::string essl = result;

    spvc_context_destroy(context);

    errc = 0;
    return essl;
}

static bool glslang_inited = false;
std::string GLSLtoGLSLES_2(const char* glsl_code, GLenum glsl_type, uint essl_version, int& return_code) {
    bool atomicCounterEmulated = false;
    std::string correct_glsl_str = preprocess_glsl(glsl_code, glsl_type, &atomicCounterEmulated);
    LOG_D("Firstly converted GLSL:\n%s", correct_glsl_str.c_str())
    int glsl_version = get_or_add_glsl_version(correct_glsl_str);

    if (!glslang_inited) {
        glslang::InitializeProcess();
        glslang_inited = true;
    }
    const char* s[] = {correct_glsl_str.c_str()};
    int errc = 0;
    std::vector<unsigned int> spirv_code = glsl_to_spirv(glsl_type, glsl_version, s, errc);
    if (errc != 0) {
        return_code = -1;
        return "";
    }
    errc = 0;
    std::string essl = spirv_to_essl(spirv_code, essl_version, errc);
    if (errc != 0) {
        return_code = -2;
        return "";
    }

    // Post-processing ESSL

    if (glsl_type != GL_COMPUTE_SHADER) {
        essl = removeLayoutBinding(essl);
    }
    essl = processOutColorLocations(essl);
    essl = forceSupporterOutput(essl);

    LOG_D("Originally GLSL to GLSL ES Complete: \n%s", essl.c_str())
    return_code = errc;
    if (return_code == 0) {
        return_code = atomicCounterEmulated ? 1 : 0;
    }
    return essl;
}

std::string GLSLtoGLSLES_1(const char* glsl_code, GLenum glsl_type, uint esversion, int& return_code) { // useless now
    /*
#if !defined(__APPLE__)
    LOG_W("Warning: use glsl optimizer to convert shader.")
    if (esversion < 300) esversion = 300;
    std::string result = MesaConvertShader(glsl_code, glsl_type == GL_VERTEX_SHADER ? GL_VERTEX_SHADER :
GL_FRAGMENT_SHADER, 460LL, esversion);

    return_code = 0;
    return result;
#else
    LOG_W_FORCE("Cannot convert glsl with version %d in MacOS/iOS", esversion);
    return std::string(glsl_code);
#endif
    */
}
