// MobileGlues - gl/drawing.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

// ============================================================================
// Drawing Module Architecture (OpenGL ES 3.2)
//
// Rule: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// Native (ES 3.2 directly supports):
//   - glDrawElements, glDrawElementsInstanced, glDrawElementsBaseVertex
//   - glDrawArrays, glDrawArraysInstanced, glDrawRangeElements
//   - glDrawArraysIndirect, glDrawElementsIndirect
//   - glClear
// ============================================================================

#include "drawing.h"
#include "buffer.h"
#include "framebuffer.h"
#include "mg.h"
#include "texture.h"
#include <ankerl/unordered_dense.h>

#define DEBUG 0

// Texture binding tracking per unit (avoids glGetIntegerv GPU round-trip).
// Updated by glBindTexture in texture.cpp.
GLuint g_tracked_tex2d_binding[32] = {0};

// ============================================================================
// Module-level state: sampler buffer emulation
// ============================================================================

GLuint bufSampelerProg;
GLuint bufSampelerLoc;
std::string bufSampelerName;

extern UnorderedMap<GLuint, bool> program_map_is_sampler_buffer_emulated;
extern UnorderedMap<GLuint, bool> program_map_is_atomic_counter_emulated;

UnorderedMap<GLuint, SamplerInfo> g_samplerCacheForSamplerBuffer;

// Cache for prepareForDraw: skip redundant work when program hasn't changed
static GLuint g_lastPreparedProgram = 0;

// ============================================================================
// Internal helpers
// ============================================================================

void setupBufferTextureUniforms(GLuint program) {
    LOG_D("setupBufferTextureUniforms, program: %d", program);

    if (!program_map_is_sampler_buffer_emulated[program]) return;

    auto it = g_samplerCacheForSamplerBuffer.find(program);
    if (it == g_samplerCacheForSamplerBuffer.end()) {
        auto& progSamplerInfo = g_samplerCacheForSamplerBuffer[program];
        GLint locWidth = GLES.glGetUniformLocation(program, "u_BufferTexWidth");
        GLint locHeight = GLES.glGetUniformLocation(program, "u_BufferTexHeight");
        if (locWidth == -1) {
            LOG_W("u_BufferTexWidth uniform not found in program %d", program);
            return;
        }

        progSamplerInfo.locHeight = locHeight;
        progSamplerInfo.locWidth = locWidth;
        progSamplerInfo.samplers.clear();

        GLint numUniforms = 0;
        GLES.glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
        LOG_D("Program %d has %d active uniforms", program, numUniforms);

        for (GLint i = 0; i < numUniforms; ++i) {
            const GLsizei bufSize = 256;
            GLchar name[bufSize];
            GLsizei length = 0;
            GLint size = 0;
            GLenum type = 0;
            GLES.glGetActiveUniform(program, i, bufSize, &length, &size, &type, name);

            if (type == GL_SAMPLER_2D || type == GL_INT_SAMPLER_2D) {
                GLint locSampler = GLES.glGetUniformLocation(program, name);
                progSamplerInfo.samplers.push_back(locSampler);
            }
        }
        it = g_samplerCacheForSamplerBuffer.find(program);
    }

    auto& progSamplerInfo = it->second;

    GLint locWidth = progSamplerInfo.locWidth;
    GLint locHeight = progSamplerInfo.locHeight;

    for (auto locSampler : progSamplerInfo.samplers) {
        if (locSampler < 0) {
            continue;
        }

        GLuint prev_unit = gl_state->current_tex_unit;
        const GLint unit = 15;

        GLES.glActiveTexture(GL_TEXTURE0 + unit);
        GLuint texId = g_tracked_tex2d_binding[unit];
        if (texId == 0) {
            GLES.glActiveTexture(GL_TEXTURE0 + prev_unit);
            continue;
        }

        auto texObject = mgGetTexObjectByID(texId);

        GLES.glUniform1i(locSampler, unit);
        GLES.glUniform1i(locWidth, texObject->width);
        GLES.glUniform1i(locHeight, texObject->height);

        GLES.glActiveTexture(GL_TEXTURE0 + prev_unit);
    }
}

// ============================================================================
// Common draw preparation (called before every draw call)
// ============================================================================

void prepareForDraw() {
    // Fast path: if texture buffer emulation is disabled, nothing to do
    if (!hardware->emulate_texture_buffer) [[likely]] return;

    // Fast path: skip if same program already prepared
    GLuint prog = gl_state->current_program;
    if (prog == g_lastPreparedProgram) [[likely]] return;
    g_lastPreparedProgram = prog;

    LOG_D("prepareForDraw...")
    setupBufferTextureUniforms(prog);
}

// ============================================================================
// Native draw calls (ES 3.2 directly supports)
// ============================================================================

// --- glDrawElements (native) ---
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    LOG()
    LOG_D("glDrawElements, mode: %d, count: %d, type: %d, indices: %p", mode, count, type, indices)
    prepareForDraw();
    GLES.glDrawElements(mode, count, type, indices);
    CHECK_GL_ERROR
}

// --- glDrawElementsInstanced (native) ---
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount) {
    LOG()
    LOG_D("glDrawElementsInstanced, mode: %d, count: %d, type: %d, indices: %p, primcount: %d", mode, count, type,
          indices, primcount)
    prepareForDraw();
    GLES.glDrawElementsInstanced(mode, count, type, indices, primcount);
    CHECK_GL_ERROR
}

// --- glDrawElementsBaseVertex (native) ---
void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex) {
    LOG()
    LOG_D("glDrawElementsBaseVertex, mode: %d, count: %d, type: %d, indices: %p, basevertex: %d", mode, count, type,
          indices, basevertex);
    prepareForDraw();
    GLES.glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
    CHECK_GL_ERROR
}

// --- glDrawArrays (native) ---
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    LOG()
    LOG_D("glDrawArrays, mode: %d, first: %d, count: %d", mode, first, count)
    prepareForDraw();
    GLES.glDrawArrays(mode, first, count);
    CHECK_GL_ERROR
}

// --- glDrawArraysInstanced (native) ---
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) {
    LOG()
    LOG_D("glDrawArraysInstanced, mode: %d, first: %d, count: %d, instancecount: %d", mode, first, count, instancecount)
    prepareForDraw();
    GLES.glDrawArraysInstanced(mode, first, count, instancecount);
    CHECK_GL_ERROR
}

// --- glDrawRangeElements (native) ---
void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices) {
    LOG()
    LOG_D("glDrawRangeElements, mode: %d, start: %d, end: %d, count: %d, type: %d, indices: %p", mode, start, end,
          count, type, indices)
    prepareForDraw();
    GLES.glDrawRangeElements(mode, start, end, count, type, indices);
    CHECK_GL_ERROR
}

// --- glDrawArraysIndirect (native) ---
void glDrawArraysIndirect(GLenum mode, const void* indirect) {
    LOG()
    LOG_D("glDrawArraysIndirect, mode: %d, indirect: %p", mode, indirect)
    prepareForDraw();
    GLES.glDrawArraysIndirect(mode, indirect);
    CHECK_GL_ERROR
}

// --- glDrawElementsIndirect (native) ---
void glDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect) {
    LOG()
    LOG_D("glDrawElementsIndirect, mode: %d, type: %d, indirect: %p", mode, type, indirect)
    prepareForDraw();
    GLES.glDrawElementsIndirect(mode, type, indirect);
    CHECK_GL_ERROR
}

// ============================================================================
// Other functions (keep existing logic)
// ============================================================================

void glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access,
                        GLenum format) {
    LOG()
    LOG_D("glBindImageTexture, unit: %d, texture: %d, level: %d, layered: %d, layer: %d, access: %d, format: %d", unit,
          texture, level, layered, layer, access, format)
    GLES.glBindImageTexture(unit, texture, level, layered, layer, access, format);
    CHECK_GL_ERROR
}

// glUniform1i moved to gl_native.cpp as a native pass-through

void bindAllAtomicCounterAsSSBO();
void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) {
    LOG()
    LOG_D("glDispatchCompute, num_groups_x: %d, num_groups_y: %d, num_groups_z: %d", num_groups_x, num_groups_y,
          num_groups_z)
    if (program_map_is_atomic_counter_emulated[gl_state->current_program]) {
        bindAllAtomicCounterAsSSBO();
        LOG_D("Atomic counters bound as SSBOs for program %d", gl_state->current_program);
    } else {
        LOG_D("No atomic counters bound as SSBOs for program %d", gl_state->current_program);
    }
    GLES.glDispatchCompute(num_groups_x, num_groups_y, num_groups_z);
    CHECK_GL_ERROR
}

void glMemoryBarrier(GLbitfield barriers) {
    LOG()
    LOG_D("glMemoryBarrier, barriers: %d", barriers)
    if (program_map_is_atomic_counter_emulated[gl_state->current_program]) {
        barriers |= GL_ATOMIC_COUNTER_BARRIER_BIT;
        barriers |= GL_SHADER_STORAGE_BARRIER_BIT;
    }
    GLES.glMemoryBarrier(barriers);
    CHECK_GL_ERROR
}