// MobileGlues - gl/mg.h
// State manager header: backward-compatible wrappers around GLStateManager.
// All old extern declarations and macros are mapped to GLStateManager methods.
//
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#pragma once

#include "state.h"

// Include the loader handle extern (defined in gles/loader.cpp)
extern void* g_loader_handle;

// ============================================================================
// Backward-compatible #define macros for old code
// ============================================================================

// Old state variable aliases (pointer-style for backward compat)
#define gl_state         (&GLState)
#define hardware         (&GLState)
#define proxy_width      GLState.proxyWidth
#define proxy_height     GLState.proxyHeight
#define proxy_intformat  GLState.proxyInternalFormat
#define current_program  GLState.currentProgram
#define current_tex_unit GLState.currentTexUnit
#define current_draw_fbo GLState.currentDrawFBO
#define current_read_fbo GLState.framebuffer.readFBO
#define is_draw_call     GLState.isDrawCall

// Old buffer map aliases
#define buffer_map          GLState.buffer.bufferMap
#define buffer_map_remove   GLState.buffer.bufferMapReverse
#define vao_map             GLState.buffer.vaoMap
#define vao_map_remove      GLState.buffer.vaoMapReverse
#define buffer_info         GLState.buffer.bufferInfo

// Old texture map aliases
#define texture_map         GLState.texture.textureMap
#define texture_map_remove  GLState.texture.textureMapReverse

// Old FBO map aliases
#define fbo_map             GLState.framebuffer.fboMap
#define fbo_map_remove      GLState.framebuffer.fboMapReverse

// Old shader/program map aliases
#define shader_map          GLState.shader.shaderMap
#define shader_map_remove   GLState.shader.shaderMapReverse
#define program_map         GLState.shader.programMap
#define program_map_remove  GLState.shader.programMapReverse

// Old sampler buffer aliases
#define tex_buffers         GLState.buffer.texBuffers

// Old format conversion aliases (inline functions to avoid macro collisions)
inline GLenum CheckTextureTarget(GLenum target) { return GLStateManager::ConvertTextureTarget(target); }
inline GLenum CheckInternalFormat(GLenum format) { return GLStateManager::ConvertInternalFormat(format); }
inline GLenum CheckFormat(GLenum format) { return GLStateManager::ConvertFormat(format); }
inline GLenum CheckType(GLenum type) { return GLStateManager::ConvertType(type); }
inline bool mglIsDepthStencil(GLenum format) { return GLStateManager::IsDepthStencilFormat(format); }
inline bool mglIsCompressed(GLenum format) { return GLStateManager::IsCompressedFormat(format); }
inline GLenum TextureBindingTarget(GLenum target) { return GLStateManager::GetTextureBindingTarget(target); }
inline GLenum targetToBinding(GLenum target) { return GLStateManager::TargetToBindingTarget(target); }

// Old state setter macros → direct assignments
#define FUNC_GL_STATE_SIZEI(name, value) \
    do { GLState.name = (value); STATE_LOG(#name " = %d", (int)(value)); } while (0)

#define FUNC_GL_STATE_ENUM(name, value) \
    do { GLState.name = (value); STATE_LOG(#name " = 0x%X", (unsigned)(value)); } while (0)

#define FUNC_GL_STATE_UINT(name, value) \
    do { GLState.name = (value); STATE_LOG(#name " = %u", (unsigned)(value)); } while (0)

// Convenience: getter for state values
#define GET_GL_STATE_SIZEI(name)    GLState.name
#define GET_GL_STATE_ENUM(name)     GLState.name
#define GET_GL_STATE_UINT(name)     GLState.name

// ============================================================================
// Utility: get real GLES ID from virtual ID
// ============================================================================

inline GLuint getRealBuffer(GLuint virtualId) {
    return GLState.GetRealBuffer(virtualId);
}

inline GLuint getVirtualBuffer(GLuint realId) {
    return GLState.GetVirtualBuffer(realId);
}

inline GLuint getRealTexture(GLuint virtualId) {
    return GLState.GetRealTexture(virtualId);
}

inline GLuint getVirtualTexture(GLuint realId) {
    return GLState.GetVirtualTexture(realId);
}

inline GLuint getRealFBO(GLuint virtualId) {
    return GLState.GetRealFBO(virtualId);
}

inline GLuint getRealVAO(GLuint virtualId) {
    return GLState.GetRealVAO(virtualId);
}

inline GLuint getRealProgram(GLuint virtualId) {
    return GLState.GetRealProgram(virtualId);
}

inline GLuint getRealShader(GLuint virtualId) {
    return GLState.GetRealShader(virtualId);
}

// ============================================================================
// State initialization (called from mg.cpp)
// ============================================================================

void StateInit();

// ============================================================================
// Global texture unit count (queried from GLES)
// ============================================================================

extern int MG_MAX_COMBINED_TEXTURE_IMAGE_UNITS;