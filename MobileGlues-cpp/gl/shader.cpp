// MobileGlues - gl/shader.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

// ============================================================================
// Shader Compilation Architecture
//
// Principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// ES 3.2 natively supports:
//   - GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER
//   - glCreateShader, glCompileShader, glShaderSource
//
// ES 3.2 does NOT natively support:
//   - Desktop GLSL (non-ESSL) shader sources
//     → CPU simulation: GLSL → GLSL ES conversion via GLSLtoGLSLES()
//   - GL_GEOMETRY_SHADER, GL_TESS_CONTROL_SHADER, GL_TESS_EVALUATION_SHADER
//     → Detected and rejected (no ES 3.2 equivalent)
//
// Conversion Pipeline:
//   1. glCreateShader(type)           → native GLES 3.2 pass-through
//   2. glShaderSource(shader, source) → if ESSL 100/300/310/320: native pass-through
//                                       otherwise: GLSL→ESSL (version 320) conversion
//   3. glCompileShader(shader)        → native GLES 3.2 pass-through
// ============================================================================

#include "shader.h"
#include "../includes.h"
#include "glsl/glsl_for_es.h"
#include "log.h"
#include "../gles/loader.h"
#include "mg.h"
#include "GL/gl.h"
#include <regex>
#include <cstring>
#include <string>

#define DEBUG 0

// ============================================================================
// Section: Shader Type Detection Helpers
//
// These helpers analyze GLSL source code to determine whether a shader
// is already ES-compatible (direct pass-through) or requires conversion.
// They also detect unsupported shader types (geometry/tessellation).
// All functions use strstr() on raw C strings to avoid std::string copies.
// ============================================================================

// Fast strstr-based helpers (no std::string allocation)
static inline bool has_str(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != nullptr;
}

// ---------------------------------------------------------------------------
// Determine if shader is already ES-compatible (direct passthrough)
// ESSL versions recognized: 100 (ES 1.0), 300 (ES 3.0), 310 (ES 3.1), 320 (ES 3.2)
// ---------------------------------------------------------------------------
static bool is_direct_shader(const char* glsl_code) {
    int version = getGLSLVersion(glsl_code);
    return (version == 100 || version == 300 || version == 310 || version == 320);
}

// ---------------------------------------------------------------------------
// Determine if shader is a geometry shader based on content analysis
// Geometry shaders are NOT supported by ES 3.2 — detected for rejection
// ---------------------------------------------------------------------------
static bool is_geometry_shader(const char* glsl_code) {
    // Check for max_vertices first (most unique identifier)
    if (has_str(glsl_code, "max_vertices")) return true;
    if (has_str(glsl_code, "EmitVertex()")) return true;
    if (has_str(glsl_code, "EndPrimitive()")) return true;
    // Check layout qualifiers
    if (has_str(glsl_code, "layout(triangles)")) return true;
    if (has_str(glsl_code, "layout(triangle_strip)")) return true;
    if (has_str(glsl_code, "layout(points)")) return true;
    if (has_str(glsl_code, "layout(lines)")) return true;
    if (has_str(glsl_code, "layout(line_strip)")) return true;
    if (has_str(glsl_code, "layout(quads)")) return true;
    if (has_str(glsl_code, "layout(isolines)")) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Determine if shader is a tessellation shader based on content analysis
// Tessellation shaders are NOT supported by ES 3.2 — detected for rejection
// ---------------------------------------------------------------------------
static bool is_tessellation_shader(const char* glsl_code) {
    if (has_str(glsl_code, "gl_TessLevel")) return true;
    if (has_str(glsl_code, "gl_TessCoord")) return true;
    if (has_str(glsl_code, "layout(vertices")) return true;
    if (has_str(glsl_code, "patch")) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Determine if shader is a compute shader based on content analysis
// Compute shaders ARE supported by ES 3.2 — detected to identify shader type
// ---------------------------------------------------------------------------
static bool is_compute_shader(const char* glsl_code) {
    if (has_str(glsl_code, "layout(local_size_x")) return true;
    if (has_str(glsl_code, "gl_WorkGroupSize")) return true;
    if (has_str(glsl_code, "gl_WorkGroupID")) return true;
    if (has_str(glsl_code, "gl_LocalInvocationID")) return true;
    if (has_str(glsl_code, "gl_GlobalInvocationID")) return true;
    if (has_str(glsl_code, "gl_NumWorkGroups")) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Detect the actual shader type from GLSL source content
// Used when the shader type was not explicitly set (e.g. Iris sets it later)
// ---------------------------------------------------------------------------
static GLenum detect_shader_type_from_source(const char* glsl_code) {
    if (is_geometry_shader(glsl_code)) return GL_GEOMETRY_SHADER;
    if (is_tessellation_shader(glsl_code)) return GL_TESS_EVALUATION_SHADER;
    if (is_compute_shader(glsl_code)) return GL_COMPUTE_SHADER;
    return GL_FRAGMENT_SHADER; // default
}

// ============================================================================
// Section: Native ES 3.2 Pass-Through Functions
//
// These functions are natively supported by OpenGL ES 3.2 and are passed
// through directly to the underlying GLES implementation without any
// CPU-side conversion or emulation.
// ============================================================================

// ---------------------------------------------------------------------------
// glCreateShader - Creates a shader object of the given type (native ES 3.2)
// ES 3.2 natively supports: VERTEX_SHADER, FRAGMENT_SHADER, COMPUTE_SHADER
// ---------------------------------------------------------------------------
NATIVE_FUNCTION_HEAD(GLuint, glCreateShader, GLenum type)
NATIVE_FUNCTION_END(GLuint, glCreateShader, type)

// ---------------------------------------------------------------------------
// glCompileShader - Compiles a shader object (native ES 3.2)
// The shader source must already be valid ESSL — conversion is handled by
// glShaderSource before this function is called.
// ---------------------------------------------------------------------------
NATIVE_FUNCTION_HEAD(void, glCompileShader, GLuint shader)
NATIVE_FUNCTION_END_NO_RETURN(void, glCompileShader, shader)

// ============================================================================
// Section: GLSL-to-GLSL-ES Conversion Pipeline
//
// glShaderSource is the entry point for shader source code. It implements
// the CPU simulation layer for non-ES GLSL code by converting desktop GLSL
// to GLSL ES 3.2 (version 320) using the GLSLtoGLSLES() conversion engine.
//
// Pipeline:
//   1. If shader is already ESSL (100/300/310/320) → native pass-through
//   2. If shader type is unknown → detect from source content
//   3. Convert GLSL → GLSL ES 3.2 via GLSLtoGLSLES()
//   4. On conversion failure → fall back to original source
// ============================================================================

void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
    LOG()
    LOG_D("glShaderSource hook, shader=%d, count=%d", shader, count)

    // Step 1: Query the shader type (set during glCreateShader)
    GLenum shaderType = GL_FRAGMENT_SHADER;
    GLES.glGetShaderiv(shader, GL_SHADER_TYPE, (GLint*)&shaderType);

    if (count <= 0 || !string || !string[0]) {
        GLES.glShaderSource(shader, count, string, length);
        return;
    }

    std::string glsl_code = string[0];

    // Step 2: If shader type is unknown (e.g. Iris sets it after source), detect from content
    if (shaderType == 0 || shaderType == GL_FRAGMENT_SHADER) {
        GLenum detected = detect_shader_type_from_source(glsl_code.c_str());
        if (detected != GL_FRAGMENT_SHADER) {
            shaderType = detected;
            LOG_D("Detected shader type from source: %d", shaderType)
        }
    }

    LOG_D("glShaderSource: shaderType=%d", shaderType)

    // Step 3: Check if already an ES-compatible shader → native pass-through
    if (is_direct_shader(glsl_code.c_str())) {
        LOG_D("Direct ES shader (ESSL 100/300/310/320), passing through")
        GLES.glShaderSource(shader, count, string, length);
        return;
    }

    // Step 4: Convert desktop GLSL → GLSL ES 3.2 (version 320)
    uint essl_version = 320; // target ES 3.2
    int return_code = -1;
    std::string converted = GLSLtoGLSLES(glsl_code.c_str(), shaderType, essl_version, 0, return_code);

    if (return_code < 0) {
        // Conversion failed — fall back to original source
        LOG_E("GLSL conversion failed, passing through original")
        GLES.glShaderSource(shader, count, string, length);
        return;
    }

    LOG_D("Converted GLSL ES:\n%s", converted.c_str())

    // Step 5: Set the converted GLSL ES source
    const char* converted_code = converted.c_str();
    GLES.glShaderSource(shader, 1, &converted_code, nullptr);
}