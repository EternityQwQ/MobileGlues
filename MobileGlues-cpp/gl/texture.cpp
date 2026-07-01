// MobileGlues - gl/texture.cpp
// Texture state management: ID mapping, unit-tracking, format conversion,
// 1D→2D texture remapping, pixel store state, and BGRA conversion.
//
// Architecture principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "../includes.h"
#include <GL/gl.h>
#include "glcorearb.h"
#include "log.h"
#include "../gles/loader.h"
#include "mg.h"
#include <GLES3/gl32.h>
#include <cstring>
#include <vector>

#define DEBUG 0

// ============================================================================
// Texture ID management
// ============================================================================

void glGenTextures(GLsizei n, GLuint *textures) {
    LOG()
    GLES.glGenTextures(n, textures);

    auto &ts = GLState.texture;
    for (GLsizei i = 0; i < n; i++) {
        TextureObject obj;
        obj.id = textures[i];
        ts.textureMap[textures[i]] = obj;
        ts.textureMapReverse[textures[i]] = textures[i];
    }
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    LOG()
    GLES.glDeleteTextures(n, textures);

    auto &ts = GLState.texture;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = textures[i];
        if (id == 0) continue;

        ts.textureMap.erase(id);
        ts.textureMapReverse.erase(id);

        for (int unit = 0; unit < MAX_TEXTURE_UNITS; unit++) {
            for (auto &pair : ts.texUnits[unit].binding) {
                if (pair.second == id) pair.second = 0;
            }
        }
    }
}

// ============================================================================
// glActiveTexture - track active texture unit
// ============================================================================

void glActiveTexture(GLenum texture) {
    LOG()
    GLES.glActiveTexture(texture);
    GLState.texture.activeUnit = texture - GL_TEXTURE0;
    GLState.currentTexUnit = GLState.texture.activeUnit;
}

// ============================================================================
// glBindTexture - ID mapping and 1D→2D conversion
// ============================================================================

void glBindTexture(GLenum target, GLuint texture) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glBindTexture(esTarget, texture);

    int unit = GLState.texture.activeUnit;
    GLenum bindingTarget = GLStateManager::TargetToBindingTarget(target);
    GLState.texture.texUnits[unit].binding[bindingTarget] = texture;

    if (texture != 0) {
        auto *info = GLState.GetTextureInfo(texture);
        if (info) {
            info->target = esTarget;
        }
    }
}

// ============================================================================
// glTexImage2D - format conversion
// ============================================================================

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void *pixels) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLenum esInternalFormat = GLStateManager::ConvertInternalFormat(internalformat);
    GLenum esFormat = GLStateManager::ConvertFormat(format);
    GLenum esType = GLStateManager::ConvertType(type);

    if (target == GL_TEXTURE_1D || target == GL_PROXY_TEXTURE_1D) {
        height = 1;
    }

    GLES.glTexImage2D(esTarget, level, esInternalFormat, width, height, border, esFormat, esType, pixels);

    int unit = GLState.texture.activeUnit;
    GLenum bindingTarget = GLStateManager::TargetToBindingTarget(target);
    GLuint boundTex = GLState.texture.texUnits[unit].binding[bindingTarget];
    if (boundTex) {
        auto *info = GLState.GetTextureInfo(boundTex);
        if (info) {
            info->target = esTarget;
            info->internalFormat = esInternalFormat;
            info->width = width;
            info->height = (target == GL_TEXTURE_1D) ? 1 : height;
            info->isDepthStencil = GLStateManager::IsDepthStencilFormat(internalformat);
            info->isCompressed = false;
        }
    }
}

void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const void *pixels) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLenum esInternalFormat = GLStateManager::ConvertInternalFormat(internalformat);
    GLenum esFormat = GLStateManager::ConvertFormat(format);
    GLenum esType = GLStateManager::ConvertType(type);

    GLES.glTexImage3D(esTarget, level, esInternalFormat, width, height, depth, border, esFormat, esType, pixels);

    int unit = GLState.texture.activeUnit;
    GLenum bindingTarget = GLStateManager::TargetToBindingTarget(target);
    GLuint boundTex = GLState.texture.texUnits[unit].binding[bindingTarget];
    if (boundTex) {
        auto *info = GLState.GetTextureInfo(boundTex);
        if (info) {
            info->target = esTarget;
            info->internalFormat = esInternalFormat;
            info->width = width;
            info->height = height;
            info->depth = depth;
            info->isDepthStencil = GLStateManager::IsDepthStencilFormat(internalformat);
            info->isCompressed = false;
        }
    }
}

// ============================================================================
// glTexSubImage2D / glTexSubImage3D
// ============================================================================

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLenum esFormat = GLStateManager::ConvertFormat(format);
    GLenum esType = GLStateManager::ConvertType(type);

    if (target == GL_TEXTURE_1D) {
        height = 1;
        yoffset = 0;
    }

    GLES.glTexSubImage2D(esTarget, level, xoffset, yoffset, width, height, esFormat, esType, pixels);
}

void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
                     GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLenum esFormat = GLStateManager::ConvertFormat(format);
    GLenum esType = GLStateManager::ConvertType(type);

    GLES.glTexSubImage3D(esTarget, level, xoffset, yoffset, zoffset, width, height, depth, esFormat, esType, pixels);
}

// ============================================================================
// glCopyTexImage2D / glCopyTexSubImage2D
// ============================================================================

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLenum esInternalFormat = GLStateManager::ConvertInternalFormat(internalformat);

    if (target == GL_TEXTURE_1D) {
        height = 1;
    }

    GLES.glCopyTexImage2D(esTarget, level, esInternalFormat, x, y, width, height, border);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);

    if (target == GL_TEXTURE_1D) {
        height = 1;
        yoffset = 0;
    }

    GLES.glCopyTexSubImage2D(esTarget, level, xoffset, yoffset, x, y, width, height);
}

void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glCopyTexSubImage3D(esTarget, level, xoffset, yoffset, zoffset, x, y, width, height);
}

// ============================================================================
// glTexStorage2D / glTexStorage3D
// ============================================================================

void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLenum esInternalFormat = GLStateManager::ConvertInternalFormat(internalformat);

    if (target == GL_TEXTURE_1D) {
        height = 1;
    }

    GLES.glTexStorage2D(esTarget, levels, esInternalFormat, width, height);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLenum esInternalFormat = GLStateManager::ConvertInternalFormat(internalformat);
    GLES.glTexStorage3D(esTarget, levels, esInternalFormat, width, height, depth);
}

// ============================================================================
// Compressed texture functions
// ============================================================================

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    if (target == GL_TEXTURE_1D) height = 1;
    GLES.glCompressedTexImage2D(esTarget, level, internalformat, width, height, border, imageSize, data);
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    if (target == GL_TEXTURE_1D) { height = 1; yoffset = 0; }
    GLES.glCompressedTexSubImage2D(esTarget, level, xoffset, yoffset, width, height, format, imageSize, data);
}

void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glCompressedTexImage3D(esTarget, level, internalformat, width, height, depth, border, imageSize, data);
}

void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
                               GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glCompressedTexSubImage3D(esTarget, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}

// ============================================================================
// glTexParameter - parameter conversion
// ============================================================================

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glTexParameterf(esTarget, pname, param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);

    GLint esParam = param;
    switch (pname) {
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
            if (param == GL_CLAMP) esParam = GL_CLAMP_TO_EDGE;
            break;
    }

    GLES.glTexParameteri(esTarget, pname, esParam);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glTexParameterfv(esTarget, pname, params);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);

    GLint esParam[4];
    memcpy(esParam, params, sizeof(GLint) * 4);
    switch (pname) {
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
            if (esParam[0] == GL_CLAMP) esParam[0] = GL_CLAMP_TO_EDGE;
            break;
    }

    GLES.glTexParameteriv(esTarget, pname, esParam);
}

// ============================================================================
// glGetTexParameter
// ============================================================================

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glGetTexParameterfv(esTarget, pname, params);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glGetTexParameteriv(esTarget, pname, params);
}

// ============================================================================
// glGetTexLevelParameter (proxy texture handling)
// ============================================================================

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
    if (target == GL_PROXY_TEXTURE_1D || target == GL_PROXY_TEXTURE_2D ||
        target == GL_PROXY_TEXTURE_3D || target == GL_PROXY_TEXTURE_CUBE_MAP) {
        switch (pname) {
            case GL_TEXTURE_WIDTH:      *params = 4096; break;
            case GL_TEXTURE_HEIGHT:     *params = 4096; break;
            case GL_TEXTURE_DEPTH:      *params = 256; break;
            case GL_TEXTURE_INTERNAL_FORMAT: *params = GL_RGBA8; break;
            case GL_TEXTURE_RED_SIZE:   *params = 8; break;
            case GL_TEXTURE_GREEN_SIZE: *params = 8; break;
            case GL_TEXTURE_BLUE_SIZE:  *params = 8; break;
            case GL_TEXTURE_ALPHA_SIZE: *params = 8; break;
            case GL_TEXTURE_COMPRESSED: *params = GL_FALSE; break;
            default:                    *params = 0; break;
        }
        return;
    }

    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    if (target == GL_TEXTURE_1D) {
        if (pname == GL_TEXTURE_HEIGHT) {
            *params = 1;
            return;
        }
    }
    GLES.glGetTexLevelParameteriv(esTarget, level, pname, params);
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
    if (target == GL_PROXY_TEXTURE_1D || target == GL_PROXY_TEXTURE_2D ||
        target == GL_PROXY_TEXTURE_3D || target == GL_PROXY_TEXTURE_CUBE_MAP) {
        GLint intVal = 0;
        glGetTexLevelParameteriv(target, level, pname, &intVal);
        *params = (GLfloat)intVal;
        return;
    }
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glGetTexLevelParameterfv(esTarget, level, pname, params);
}

// ============================================================================
// glGenerateMipmap
// ============================================================================

void glGenerateMipmap(GLenum target) {
    LOG()
    GLenum esTarget = GLStateManager::ConvertTextureTarget(target);
    GLES.glGenerateMipmap(esTarget);
}

// ============================================================================
// glPixelStorei - track pixel store state
// ============================================================================

void glPixelStorei(GLenum pname, GLint param) {
    LOG()
    GLES.glPixelStorei(pname, param);

    auto &ts = GLState.texture;
    switch (pname) {
        case GL_UNPACK_ALIGNMENT:  ts.unpackAlignment = param; break;
        case GL_UNPACK_ROW_LENGTH: ts.unpackRowLength = param; break;
        case GL_UNPACK_IMAGE_HEIGHT: ts.unpackImageHeight = param; break;
        case GL_UNPACK_SKIP_PIXELS: ts.unpackSkipPixels = param; break;
        case GL_UNPACK_SKIP_ROWS:   ts.unpackSkipRows = param; break;
        case GL_UNPACK_SKIP_IMAGES: ts.unpackSkipImages = param; break;
        case GL_PACK_ALIGNMENT:     ts.packAlignment = param; break;
        case GL_PACK_ROW_LENGTH:    ts.packRowLength = param; break;
        case GL_PACK_IMAGE_HEIGHT:  ts.packImageHeight = param; break;
        case GL_PACK_SKIP_PIXELS:   ts.packSkipPixels = param; break;
        case GL_PACK_SKIP_ROWS:     ts.packSkipRows = param; break;
        case GL_PACK_SKIP_IMAGES:   ts.packSkipImages = param; break;
    }
}

// ============================================================================
// glReadPixels - BGRA format conversion
// ============================================================================

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) {
    LOG()
    GLenum esFormat = GLStateManager::ConvertFormat(format);
    GLenum esType = GLStateManager::ConvertType(type);

    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE) {
        std::vector<GLubyte> rgba(width * height * 4);
        GLES.glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        GLubyte *dst = (GLubyte *)pixels;
        for (GLsizei i = 0; i < width * height; i++) {
            dst[i * 4 + 0] = rgba[i * 4 + 2];
            dst[i * 4 + 1] = rgba[i * 4 + 1];
            dst[i * 4 + 2] = rgba[i * 4 + 0];
            dst[i * 4 + 3] = rgba[i * 4 + 3];
        }
    } else {
        GLES.glReadPixels(x, y, width, height, esFormat, esType, pixels);
    }
}

// ============================================================================
// glRenderbufferStorage - format conversion
// ============================================================================

void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    LOG()
    GLenum esInternalFormat = GLStateManager::ConvertInternalFormat(internalformat);
    GLES.glRenderbufferStorage(target, esInternalFormat, width, height);
}

void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {
    LOG()
    GLenum esInternalFormat = GLStateManager::ConvertInternalFormat(internalformat);
    GLES.glRenderbufferStorageMultisample(target, samples, esInternalFormat, width, height);
}

// ============================================================================
// Map initialization helpers (for backward compatibility)
// ============================================================================

void InitTextureMap(size_t expectedSize) {
    auto &ts = GLState.texture;
    ts.textureMap.reserve(expectedSize);
    ts.textureMapReverse.reserve(expectedSize);
}