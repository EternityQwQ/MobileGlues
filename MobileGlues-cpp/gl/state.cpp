// MobileGlues - gl/state.cpp
// Unified OpenGL State Manager implementation
//
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "state.h"
#include "../gles/loader.h"

// ============================================================================
// GLStateManager::Initialize
// ============================================================================

void GLStateManager::Initialize()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    STATE_LOG("GLStateManager::Initialize()");

    // Query ES version
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    esVersion = major * 10 + minor;

    // Reset all subsystems
    buffer.Reset();
    texture.Reset();
    framebuffer.Reset();
    shader.Reset();
    sampler.Reset();
    image.Reset();
    legacy.Reset();
    vertexArray.Reset();
    query.Reset();
    transformFeedback.Reset();
    renderbuffer.Reset();

    // Detect if texture buffer is supported natively
    emulateTextureBuffer = true; // Most mobile GPUs don't support TBO natively
}

void GLStateManager::Shutdown()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    STATE_LOG("GLStateManager::Shutdown()");

    // Clean up temp FBOs
    for (auto& tmp : framebuffer.tempFBOs) {
        if (tmp.fbo) glDeleteFramebuffers(1, &tmp.fbo);
        if (tmp.texture) glDeleteTextures(1, &tmp.texture);
    }
    framebuffer.tempFBOs.clear();

    buffer.Reset();
    texture.Reset();
    framebuffer.Reset();
    shader.Reset();
    sampler.Reset();
    image.Reset();
    legacy.Reset();
    vertexArray.Reset();
    query.Reset();
    transformFeedback.Reset();
    renderbuffer.Reset();
}

// ============================================================================
// Subsystem Reset Methods
// ============================================================================

void GLStateManager::BufferState::Reset()
{
    bufferMap.clear();
    bufferMapReverse.clear();
    bufferInfo.clear();
    vaoMap.clear();
    vaoMapReverse.clear();
    boundBuffer.clear();
    memset(uniformBufferBases, 0, sizeof(uniformBufferBases));
    memset(uniformBufferOffsets, 0, sizeof(uniformBufferOffsets));
    memset(uniformBufferSizes, 0, sizeof(uniformBufferSizes));
    memset(shaderStorageBases, 0, sizeof(shaderStorageBases));
    memset(atomicCounterBases, 0, sizeof(atomicCounterBases));
    memset(transformFeedbackBuffers, 0, sizeof(transformFeedbackBuffers));
    texBuffers.clear();
    bufferMaps.clear();
    atomicCounterData.clear();
    atomicCounterBufferBinding = 0;
    atomicCounterBufferSize = 0;
    atomicCounterBufferOffset = 0;
}

void GLStateManager::TextureState::Reset()
{
    textureMap.clear();
    textureMapReverse.clear();
    for (int i = 0; i < MAX_TEXTURE_UNITS; i++) {
        texUnits[i].binding.clear();
    }
    activeUnit = 0;
    unpackAlignment = 4;
    unpackRowLength = 0;
    unpackImageHeight = 0;
    unpackSkipPixels = 0;
    unpackSkipRows = 0;
    unpackSkipImages = 0;
    packAlignment = 4;
    packRowLength = 0;
    packImageHeight = 0;
    packSkipPixels = 0;
    packSkipRows = 0;
    packSkipImages = 0;
}

void GLStateManager::FramebufferState::Reset()
{
    fboMap.clear();
    fboMapReverse.clear();
    drawFBO = 0;
    readFBO = 0;
    attachments.clear();
    memset(drawBuffers, 0, sizeof(drawBuffers));
    drawBuffers[0] = GL_BACK;
    readBuffer = GL_BACK;
    drawBufferCount = 1;
    tempFBOs.clear();
    defaultFboInvalidated = false;
}

void GLStateManager::ShaderState::Reset()
{
    shaderMap.clear();
    shaderMapReverse.clear();
    programMap.clear();
    programMapReverse.clear();
    shaderInfo.clear();
    programInfo.clear();
    currentProgram = 0;
}

// ============================================================================
// Format Conversion Utilities
// ============================================================================

// Map desktop GL texture targets to GLES-compatible targets.
// 1D textures → 2D, 1D array → 2D array, rectangle → 2D, etc.
GLenum GLStateManager::ConvertTextureTarget(GLenum target)
{
    switch (target) {
        case GL_TEXTURE_1D:
        case GL_PROXY_TEXTURE_1D:
            return GL_TEXTURE_2D;
        case GL_TEXTURE_1D_ARRAY:
            return GL_TEXTURE_2D_ARRAY;
        case GL_TEXTURE_RECTANGLE:
            return GL_TEXTURE_2D;
        case GL_TEXTURE_CUBE_MAP:
        case GL_TEXTURE_2D:
        case GL_TEXTURE_3D:
        case GL_TEXTURE_2D_ARRAY:
        case GL_TEXTURE_CUBE_MAP_ARRAY:
        case GL_TEXTURE_2D_MULTISAMPLE:
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
        case GL_TEXTURE_BUFFER:
            return target;
        default:
            return GL_TEXTURE_2D;
    }
}

// Convert desktop GL internal formats to GLES-compatible equivalents.
GLenum GLStateManager::ConvertInternalFormat(GLenum format)
{
    switch (format) {
        // Legacy luminance formats → R
        case GL_LUMINANCE:           return GL_R8;
        case GL_LUMINANCE4:          return GL_R8;
        case GL_LUMINANCE8:          return GL_R8;
        case GL_LUMINANCE12:         return GL_R8;
        case GL_LUMINANCE16:         return GL_R16;
        case GL_LUMINANCE_ALPHA:     return GL_RG8;
        case GL_LUMINANCE4_ALPHA4:   return GL_RG8;
        case GL_LUMINANCE6_ALPHA2:   return GL_RG8;
        case GL_LUMINANCE8_ALPHA8:   return GL_RG8;
        case GL_LUMINANCE12_ALPHA4:  return GL_RG8;
        case GL_LUMINANCE12_ALPHA12: return GL_RG8;
        case GL_LUMINANCE16_ALPHA16: return GL_RG16;
        // Legacy intensity formats → R
        case GL_INTENSITY:           return GL_R8;
        case GL_INTENSITY4:          return GL_R8;
        case GL_INTENSITY8:          return GL_R8;
        case GL_INTENSITY12:         return GL_R8;
        case GL_INTENSITY16:         return GL_R16;
        // Legacy alpha formats → R
        case GL_ALPHA4:              return GL_R8;
        case GL_ALPHA8:              return GL_R8;
        case GL_ALPHA12:             return GL_R8;
        case GL_ALPHA16:             return GL_R16;
        // Legacy RGB formats
        case GL_R3_G3_B2:            return GL_RGB8;
        case GL_RGB4:                return GL_RGB8;
        case GL_RGB5:                return GL_RGB8;
        case GL_RGB8:                return GL_RGB8;
        case GL_RGB10:               return GL_RGB10;
        case GL_RGB12:               return GL_RGB8;
        case GL_RGB16:               return GL_RGB16;
        // Legacy RGBA formats
        case GL_RGBA2:               return GL_RGBA8;
        case GL_RGBA4:               return GL_RGBA8;
        case GL_RGB5_A1:             return GL_RGB5_A1;
        case GL_RGBA8:               return GL_RGBA8;
        case GL_RGB10_A2:            return GL_RGB10_A2;
        case GL_RGBA12:              return GL_RGBA8;
        case GL_RGBA16:              return GL_RGBA16;
        // Depth/stencil
        case GL_DEPTH24_STENCIL8:    return GL_DEPTH24_STENCIL8;
        case GL_DEPTH32F_STENCIL8:   return GL_DEPTH32F_STENCIL8;
        case GL_DEPTH_COMPONENT16:   return GL_DEPTH_COMPONENT16;
        case GL_DEPTH_COMPONENT24:   return GL_DEPTH_COMPONENT24;
        case GL_DEPTH_COMPONENT32:   return GL_DEPTH_COMPONENT32F;
        case GL_DEPTH_COMPONENT32F:  return GL_DEPTH_COMPONENT32F;
        // Compressed formats pass through
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_RED_RGTC1:
        case GL_COMPRESSED_RG_RGTC2:
        case GL_COMPRESSED_SIGNED_RED_RGTC1:
        case GL_COMPRESSED_SIGNED_RG_RGTC2:
        case GL_COMPRESSED_RGBA_BPTC_UNORM:
        case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
        case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
        case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT:
        case GL_COMPRESSED_RGB8_ETC2:
        case GL_COMPRESSED_SRGB8_ETC2:
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
        case GL_COMPRESSED_R11_EAC:
        case GL_COMPRESSED_SIGNED_R11_EAC:
        case GL_COMPRESSED_RG11_EAC:
        case GL_COMPRESSED_SIGNED_RG11_EAC:
        case GL_COMPRESSED_RGBA_ASTC_4x4_KHR:
        case GL_COMPRESSED_RGBA_ASTC_5x4_KHR:
        case GL_COMPRESSED_RGBA_ASTC_5x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_6x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_6x6_KHR:
        case GL_COMPRESSED_RGBA_ASTC_8x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_8x6_KHR:
        case GL_COMPRESSED_RGBA_ASTC_8x8_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x5_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x6_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x8_KHR:
        case GL_COMPRESSED_RGBA_ASTC_10x10_KHR:
        case GL_COMPRESSED_RGBA_ASTC_12x10_KHR:
        case GL_COMPRESSED_RGBA_ASTC_12x12_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR:
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR:
            return format;
        // sRGB
        case GL_SRGB8:               return GL_SRGB8;
        case GL_SRGB8_ALPHA8:        return GL_SRGB8_ALPHA8;
        // Integer formats
        case GL_R8I:                 return GL_R8I;
        case GL_R8UI:                return GL_R8UI;
        case GL_R16I:                return GL_R16I;
        case GL_R16UI:               return GL_R16UI;
        case GL_R32I:                return GL_R32I;
        case GL_R32UI:               return GL_R32UI;
        case GL_RG8I:                return GL_RG8I;
        case GL_RG8UI:               return GL_RG8UI;
        case GL_RG16I:               return GL_RG16I;
        case GL_RG16UI:              return GL_RG16UI;
        case GL_RG32I:               return GL_RG32I;
        case GL_RG32UI:              return GL_RG32UI;
        case GL_RGB8I:               return GL_RGB8I;
        case GL_RGB8UI:              return GL_RGB8UI;
        case GL_RGB16I:              return GL_RGB16I;
        case GL_RGB16UI:             return GL_RGB16UI;
        case GL_RGB32I:              return GL_RGB32I;
        case GL_RGB32UI:             return GL_RGB32UI;
        case GL_RGBA8I:              return GL_RGBA8I;
        case GL_RGBA8UI:             return GL_RGBA8UI;
        case GL_RGBA16I:             return GL_RGBA16I;
        case GL_RGBA16UI:            return GL_RGBA16UI;
        case GL_RGBA32I:             return GL_RGBA32I;
        case GL_RGBA32UI:            return GL_RGBA32UI;
        // Float formats
        case GL_R16F:                return GL_R16F;
        case GL_RG16F:               return GL_RG16F;
        case GL_RGB16F:              return GL_RGB16F;
        case GL_RGBA16F:             return GL_RGBA16F;
        case GL_R32F:                return GL_R32F;
        case GL_RG32F:               return GL_RG32F;
        case GL_RGB32F:              return GL_RGB32F;
        case GL_RGBA32F:             return GL_RGBA32F;
        case GL_R11F_G11F_B10F:      return GL_R11F_G11F_B10F;
        case GL_RGB9_E5:             return GL_RGB9_E5;
        // Default: pass through
        default:
            return format;
    }
}

// Convert desktop GL pixel data formats to GLES-compatible equivalents.
GLenum GLStateManager::ConvertFormat(GLenum format)
{
    switch (format) {
        case GL_LUMINANCE:           return GL_RED;
        case GL_LUMINANCE_ALPHA:     return GL_RG;
        case GL_INTENSITY:           return GL_RED;
        case GL_ALPHA:               return GL_RED;
        case GL_BGR:                 return GL_RGB;
        case GL_BGRA:                return GL_RGBA;
        case GL_RED:                 return GL_RED;
        case GL_RG:                  return GL_RG;
        case GL_RGB:                 return GL_RGB;
        case GL_RGBA:                return GL_RGBA;
        case GL_DEPTH_COMPONENT:     return GL_DEPTH_COMPONENT;
        case GL_DEPTH_STENCIL:       return GL_DEPTH_STENCIL;
        case GL_STENCIL_INDEX:       return GL_STENCIL_INDEX;
        default:                     return format;
    }
}

// Convert desktop GL types to GLES-compatible equivalents.
GLenum GLStateManager::ConvertType(GLenum type)
{
    switch (type) {
        case GL_UNSIGNED_BYTE_3_3_2:        return GL_UNSIGNED_BYTE;
        case GL_UNSIGNED_BYTE_2_3_3_REV:    return GL_UNSIGNED_BYTE;
        case GL_UNSIGNED_SHORT_5_6_5:       return GL_UNSIGNED_SHORT_5_6_5;
        case GL_UNSIGNED_SHORT_5_6_5_REV:   return GL_UNSIGNED_SHORT_5_6_5;
        case GL_UNSIGNED_SHORT_4_4_4_4:     return GL_UNSIGNED_SHORT_4_4_4_4;
        case GL_UNSIGNED_SHORT_4_4_4_4_REV: return GL_UNSIGNED_SHORT_4_4_4_4;
        case GL_UNSIGNED_SHORT_5_5_5_1:     return GL_UNSIGNED_SHORT_5_5_5_1;
        case GL_UNSIGNED_SHORT_1_5_5_5_REV: return GL_UNSIGNED_SHORT_5_5_5_1;
        case GL_UNSIGNED_INT_8_8_8_8:       return GL_UNSIGNED_BYTE;
        case GL_UNSIGNED_INT_8_8_8_8_REV:   return GL_UNSIGNED_BYTE;
        case GL_UNSIGNED_INT_10_10_10_2:    return GL_UNSIGNED_INT_10_10_10_2;
        case GL_UNSIGNED_INT_2_10_10_10_REV:return GL_UNSIGNED_INT_2_10_10_10_REV;
        case GL_UNSIGNED_BYTE:              return GL_UNSIGNED_BYTE;
        case GL_BYTE:                       return GL_BYTE;
        case GL_UNSIGNED_SHORT:             return GL_UNSIGNED_SHORT;
        case GL_SHORT:                      return GL_SHORT;
        case GL_UNSIGNED_INT:               return GL_UNSIGNED_INT;
        case GL_INT:                        return GL_INT;
        case GL_FLOAT:                      return GL_FLOAT;
        case GL_HALF_FLOAT:                 return GL_HALF_FLOAT;
        case GL_UNSIGNED_INT_24_8:          return GL_UNSIGNED_INT_24_8;
        default:                            return type;
    }
}

bool GLStateManager::IsDepthStencilFormat(GLenum format)
{
    switch (format) {
        case GL_DEPTH_COMPONENT16:
        case GL_DEPTH_COMPONENT24:
        case GL_DEPTH_COMPONENT32:
        case GL_DEPTH_COMPONENT32F:
        case GL_DEPTH24_STENCIL8:
        case GL_DEPTH32F_STENCIL8:
        case GL_DEPTH_STENCIL:
        case GL_DEPTH_COMPONENT:
            return true;
        default:
            return false;
    }
}

bool GLStateManager::IsCompressedFormat(GLenum format)
{
    switch (format) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_RED_RGTC1:
        case GL_COMPRESSED_RG_RGTC2:
        case GL_COMPRESSED_SIGNED_RED_RGTC1:
        case GL_COMPRESSED_SIGNED_RG_RGTC2:
        case GL_COMPRESSED_RGBA_BPTC_UNORM:
        case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
        case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
        case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT:
        case GL_COMPRESSED_RGB8_ETC2:
        case GL_COMPRESSED_SRGB8_ETC2:
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
        case GL_COMPRESSED_R11_EAC:
        case GL_COMPRESSED_SIGNED_R11_EAC:
        case GL_COMPRESSED_RG11_EAC:
        case GL_COMPRESSED_SIGNED_RG11_EAC:
            return true;
        default:
            return false;
    }
}

// Map a texture target to its corresponding binding query enum.
GLenum GLStateManager::GetTextureBindingTarget(GLenum target)
{
    switch (target) {
        case GL_TEXTURE_1D:       return GL_TEXTURE_BINDING_1D;
        case GL_TEXTURE_2D:       return GL_TEXTURE_BINDING_2D;
        case GL_TEXTURE_3D:       return GL_TEXTURE_BINDING_3D;
        case GL_TEXTURE_CUBE_MAP: return GL_TEXTURE_BINDING_CUBE_MAP;
        case GL_TEXTURE_1D_ARRAY: return GL_TEXTURE_BINDING_1D_ARRAY;
        case GL_TEXTURE_2D_ARRAY: return GL_TEXTURE_BINDING_2D_ARRAY;
        default:                  return GL_TEXTURE_BINDING_2D;
    }
}

// Map a texture target to its binding target enum (for tracking).
GLenum GLStateManager::TargetToBindingTarget(GLenum target)
{
    switch (target) {
        case GL_TEXTURE_1D:                 return GL_TEXTURE_1D;
        case GL_TEXTURE_2D:                 return GL_TEXTURE_2D;
        case GL_TEXTURE_3D:                 return GL_TEXTURE_3D;
        case GL_TEXTURE_CUBE_MAP:           return GL_TEXTURE_CUBE_MAP;
        case GL_TEXTURE_1D_ARRAY:           return GL_TEXTURE_1D_ARRAY;
        case GL_TEXTURE_2D_ARRAY:           return GL_TEXTURE_2D_ARRAY;
        case GL_TEXTURE_RECTANGLE:          return GL_TEXTURE_2D;
        case GL_TEXTURE_CUBE_MAP_ARRAY:     return GL_TEXTURE_CUBE_MAP_ARRAY;
        case GL_TEXTURE_2D_MULTISAMPLE:     return GL_TEXTURE_2D_MULTISAMPLE;
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        case GL_TEXTURE_BUFFER:             return GL_TEXTURE_BUFFER;
        default:                            return GL_TEXTURE_2D;
    }
}