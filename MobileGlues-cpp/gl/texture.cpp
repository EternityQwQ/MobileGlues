// MobileGlues - gl/texture.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

// ============================================================================
// Texture Module Architecture (OpenGL ES 3.2)
//
// Rule: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// Native (ES 3.2 directly supports):
//   - glTexImage2D, glTexImage3D, glTexSubImage2D, glTexSubImage3D
//   - glTexStorage2D, glTexStorage3D
//   - glCompressedTexImage2D, glCompressedTexImage3D
//   - glCompressedTexSubImage2D, glCompressedTexSubImage3D
//   - glCopyTexImage2D, glCopyTexSubImage2D, glCopyTexSubImage3D
//   - glGenerateMipmap
//   - glTexParameterf, glTexParameteri, glTexParameteriv, glTexParameterfv
//   - glGetTexParameteriv, glGetTexParameterfv
//   - glGetTexLevelParameteriv, glGetTexLevelParameterfv
//   - glTexBuffer, glTexBufferRange
//   - glBindTexture (with 1D→2D mapping for legacy targets)
// ============================================================================

#include "texture.h"
#include "GLES3/gl32.h"

#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <malloc.h>

#include "../gles/gles.h"
#include "../gles/loader.h"
#include "drawing.h"
#include "framebuffer.h"
#include "log.h"
#include "mg.h"
#include <GL/gl.h>

#define DEBUG 0

// ============================================================================
// Internal helpers: mip level size calculation
// ============================================================================

int nlevel(int size, int level) {
    if (size) {
        size >>= level;
        if (!size) size = 1;
    }
    return size;
}

// ============================================================================
// Texture target enum conversion
// ============================================================================

// Lookup table: TextureTarget → GLenum (dense enum 0..23)
static const GLenum kTextureTargetToGLEnum[] = {
    GL_TEXTURE_1D,                   // TEXTURE_1D = 0
    GL_PROXY_TEXTURE_1D,             // PROXY_TEXTURE_1D = 1
    GL_TEXTURE_1D_ARRAY,             // TEXTURE_1D_ARRAY = 2
    GL_PROXY_TEXTURE_1D_ARRAY,       // PROXY_TEXTURE_1D_ARRAY = 3
    GL_TEXTURE_2D,                   // TEXTURE_2D = 4
    GL_PROXY_TEXTURE_2D,             // PROXY_TEXTURE_2D = 5
    GL_TEXTURE_2D_ARRAY,             // TEXTURE_2D_ARRAY = 6
    GL_PROXY_TEXTURE_2D_ARRAY,       // PROXY_TEXTURE_2D_ARRAY = 7
    GL_TEXTURE_2D_MULTISAMPLE,       // TEXTURE_2D_MULTISAMPLE = 8
    GL_PROXY_TEXTURE_2D_MULTISAMPLE, // PROXY_TEXTURE_2D_MULTISAMPLE = 9
    GL_TEXTURE_2D_MULTISAMPLE_ARRAY,         // TEXTURE_2D_MULTISAMPLE_ARRAY = 10
    GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY,   // PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY = 11
    GL_TEXTURE_3D,                   // TEXTURE_3D = 12
    GL_PROXY_TEXTURE_3D,             // PROXY_TEXTURE_3D = 13
    GL_TEXTURE_RECTANGLE,            // TEXTURE_RECTANGLE = 14
    GL_PROXY_TEXTURE_RECTANGLE,      // PROXY_TEXTURE_RECTANGLE = 15
    GL_TEXTURE_CUBE_MAP,             // TEXTURE_CUBE_MAP = 16
    GL_PROXY_TEXTURE_CUBE_MAP,       // PROXY_TEXTURE_CUBE_MAP = 17
    GL_TEXTURE_CUBE_MAP_ARRAY,       // TEXTURE_CUBE_MAP_ARRAY = 18
    GL_PROXY_TEXTURE_CUBE_MAP_ARRAY, // PROXY_TEXTURE_CUBE_MAP_ARRAY = 19
    GL_TEXTURE_BUFFER,               // TEXTURE_BUFFER = 20
};

GLenum ConvertTextureTargetToGLEnum(TextureTarget target) {
    auto idx = static_cast<unsigned>(target);
    if (idx < sizeof(kTextureTargetToGLEnum) / sizeof(kTextureTargetToGLEnum[0])) [[likely]] {
        return kTextureTargetToGLEnum[idx];
    }
    return GL_TEXTURE_2D;
}

// Sorted key-value pair for binary search lookup (GLenum → TextureTarget)
struct TexTargetEntry {
    GLenum key;
    TextureTarget value;
};

static const TexTargetEntry kGLEnumToTextureTarget[] = {
    {GL_TEXTURE_1D, TextureTarget::TEXTURE_1D},
    {GL_TEXTURE_2D, TextureTarget::TEXTURE_2D},
    {GL_PROXY_TEXTURE_1D, TextureTarget::PROXY_TEXTURE_1D},
    {GL_PROXY_TEXTURE_2D, TextureTarget::PROXY_TEXTURE_2D},
    {GL_TEXTURE_3D, TextureTarget::TEXTURE_3D},
    {GL_PROXY_TEXTURE_3D, TextureTarget::PROXY_TEXTURE_3D},
    {GL_TEXTURE_RECTANGLE, TextureTarget::TEXTURE_RECTANGLE},
    {GL_PROXY_TEXTURE_RECTANGLE, TextureTarget::PROXY_TEXTURE_RECTANGLE},
    {GL_TEXTURE_CUBE_MAP, TextureTarget::TEXTURE_CUBE_MAP},
    {GL_TEXTURE_CUBE_MAP_POSITIVE_X, TextureTarget::TEXTURE_CUBE_MAP},
    {GL_TEXTURE_CUBE_MAP_NEGATIVE_X, TextureTarget::TEXTURE_CUBE_MAP},
    {GL_TEXTURE_CUBE_MAP_POSITIVE_Y, TextureTarget::TEXTURE_CUBE_MAP},
    {GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, TextureTarget::TEXTURE_CUBE_MAP},
    {GL_TEXTURE_CUBE_MAP_POSITIVE_Z, TextureTarget::TEXTURE_CUBE_MAP},
    {GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, TextureTarget::TEXTURE_CUBE_MAP},
    {GL_PROXY_TEXTURE_CUBE_MAP, TextureTarget::PROXY_TEXTURE_CUBE_MAP},
    {GL_TEXTURE_1D_ARRAY, TextureTarget::TEXTURE_1D_ARRAY},
    {GL_PROXY_TEXTURE_1D_ARRAY, TextureTarget::PROXY_TEXTURE_1D_ARRAY},
    {GL_TEXTURE_2D_ARRAY, TextureTarget::TEXTURE_2D_ARRAY},
    {GL_PROXY_TEXTURE_2D_ARRAY, TextureTarget::PROXY_TEXTURE_2D_ARRAY},
    {GL_TEXTURE_BUFFER, TextureTarget::TEXTURE_BUFFER},
    {GL_TEXTURE_CUBE_MAP_ARRAY, TextureTarget::TEXTURE_CUBE_MAP_ARRAY},
    {GL_PROXY_TEXTURE_CUBE_MAP_ARRAY, TextureTarget::PROXY_TEXTURE_CUBE_MAP_ARRAY},
    {GL_TEXTURE_2D_MULTISAMPLE, TextureTarget::TEXTURE_2D_MULTISAMPLE},
    {GL_PROXY_TEXTURE_2D_MULTISAMPLE, TextureTarget::PROXY_TEXTURE_2D_MULTISAMPLE},
    {GL_TEXTURE_2D_MULTISAMPLE_ARRAY, TextureTarget::TEXTURE_2D_MULTISAMPLE_ARRAY},
    {GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY, TextureTarget::PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY},
};

static constexpr size_t kTexTargetEntryCount = sizeof(kGLEnumToTextureTarget) / sizeof(kGLEnumToTextureTarget[0]);

TextureTarget ConvertGLEnumToTextureTarget(GLenum target) {
    // Switch-based direct lookup — faster than binary search for this hot path
    switch (target) {
    case GL_TEXTURE_1D:                   return TextureTarget::TEXTURE_1D;
    case GL_PROXY_TEXTURE_1D:             return TextureTarget::PROXY_TEXTURE_1D;
    case GL_TEXTURE_2D:                   return TextureTarget::TEXTURE_2D;
    case GL_PROXY_TEXTURE_2D:             return TextureTarget::PROXY_TEXTURE_2D;
    case GL_TEXTURE_3D:                   return TextureTarget::TEXTURE_3D;
    case GL_PROXY_TEXTURE_3D:             return TextureTarget::PROXY_TEXTURE_3D;
    case GL_TEXTURE_RECTANGLE:            return TextureTarget::TEXTURE_RECTANGLE;
    case GL_PROXY_TEXTURE_RECTANGLE:      return TextureTarget::PROXY_TEXTURE_RECTANGLE;
    case GL_TEXTURE_CUBE_MAP:             return TextureTarget::TEXTURE_CUBE_MAP;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:  return TextureTarget::TEXTURE_CUBE_MAP;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:  return TextureTarget::TEXTURE_CUBE_MAP;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:  return TextureTarget::TEXTURE_CUBE_MAP;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:  return TextureTarget::TEXTURE_CUBE_MAP;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:  return TextureTarget::TEXTURE_CUBE_MAP;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:  return TextureTarget::TEXTURE_CUBE_MAP;
    case GL_PROXY_TEXTURE_CUBE_MAP:       return TextureTarget::PROXY_TEXTURE_CUBE_MAP;
    case GL_TEXTURE_1D_ARRAY:             return TextureTarget::TEXTURE_1D_ARRAY;
    case GL_PROXY_TEXTURE_1D_ARRAY:       return TextureTarget::PROXY_TEXTURE_1D_ARRAY;
    case GL_TEXTURE_2D_ARRAY:             return TextureTarget::TEXTURE_2D_ARRAY;
    case GL_PROXY_TEXTURE_2D_ARRAY:       return TextureTarget::PROXY_TEXTURE_2D_ARRAY;
    case GL_TEXTURE_BUFFER:               return TextureTarget::TEXTURE_BUFFER;
    case GL_TEXTURE_CUBE_MAP_ARRAY:       return TextureTarget::TEXTURE_CUBE_MAP_ARRAY;
    case GL_PROXY_TEXTURE_CUBE_MAP_ARRAY: return TextureTarget::PROXY_TEXTURE_CUBE_MAP_ARRAY;
    case GL_TEXTURE_2D_MULTISAMPLE:       return TextureTarget::TEXTURE_2D_MULTISAMPLE;
    case GL_PROXY_TEXTURE_2D_MULTISAMPLE: return TextureTarget::PROXY_TEXTURE_2D_MULTISAMPLE;
    case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:         return TextureTarget::TEXTURE_2D_MULTISAMPLE_ARRAY;
    case GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY:   return TextureTarget::PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY;
    default:                              return TextureTarget::UNKNWON;
    }
}

// ============================================================================
// Texture map management (tracking bound texture objects)
// ============================================================================

const int MAX_TEXTURE_IMAGE_UNITS = 32;

class TextureBindingSlot {
public:
    using TargetEnum = TextureTarget;

    TextureBindingSlot() : m_target((TargetEnum)0), m_boundObject(nullptr) {}

    explicit TextureBindingSlot(TargetEnum target) : m_target(target), m_boundObject(nullptr) {}

    void Bind(TextureObject* object) { m_boundObject = object; }

    TextureObject* GetBoundObject() const { return m_boundObject; }

    TargetEnum GetTarget() const { return m_target; }

private:
    TargetEnum m_target;
    TextureObject* m_boundObject;
};

class TextureUnit {
public:
    TextureBindingSlot& GetBindingSlot(TextureBindingSlot::TargetEnum target) { return m_slots[(int)target]; }

private:
    std::array<TextureBindingSlot, (int)TextureTarget::TEXTURES_COUNT> m_slots;
};

static std::vector<TextureObject*> BufferObjectsVec;
static std::array<TextureUnit, MAX_TEXTURE_IMAGE_UNITS> TextureUnits;
static int CurrentTextureUnitIndex = 0;

void InitTextureMap(size_t expectedSize) {
    BufferObjectsVec.reserve(expectedSize);
}

TextureObject* GetOrCreateTextureObject(GLuint index) {
    if (index >= BufferObjectsVec.size()) {
        BufferObjectsVec.resize(index + 1, nullptr);
    }

    auto& obj = BufferObjectsVec[index];
    if (!obj) {
        obj = new TextureObject();
        obj->texture = index;
    }
    return obj;
}

void ActivateTextureUnit(int unit) {
    if (unit < 0 || unit >= MAX_TEXTURE_IMAGE_UNITS) {
        LOG_E("Invalid texture unit: %d", unit);
        return;
    }
    CurrentTextureUnitIndex = unit;
}

int GetCurrentTextureUnitIndex() {
    return CurrentTextureUnitIndex;
}

TextureUnit& GetTextureUnit(int unit) {
    if (unit < 0 || unit >= MAX_TEXTURE_IMAGE_UNITS) {
        LOG_E("Invalid texture unit: %d", unit);
        return TextureUnits[0];
    }
    return TextureUnits[unit];
}

void MarkTextureObjectForDeletion(unsigned texture) {
    if (texture >= BufferObjectsVec.size() || !BufferObjectsVec[texture]) {
        LOG_D("Texture %u not found in BufferObjectsVec!", texture);
        return;
    }

    auto textureObject = BufferObjectsVec[texture];

    for (auto& unit : TextureUnits) {
        auto& bindingSlot = unit.GetBindingSlot(textureObject->target);
        if (bindingSlot.GetBoundObject() == textureObject) {
            bindingSlot.Bind(nullptr);
        }
    }

    BufferObjectsVec[texture] = nullptr;
    delete textureObject;
}

TextureObject* mgGetTexObjectByTarget(GLenum target) {
    return GetTextureUnit(GetCurrentTextureUnitIndex())
        .GetBindingSlot(ConvertGLEnumToTextureTarget(target))
        .GetBoundObject();
}

TextureObject* mgGetTexObjectByID(unsigned texture) {
    if (texture >= BufferObjectsVec.size() || !BufferObjectsVec[texture]) {
        LOG_E("Texture %u not found in BufferObjectsVec!", texture);
        return nullptr;
    }
    return BufferObjectsVec[texture];
}

// ============================================================================
// Internal format conversion helper
// ============================================================================

// ============================================================================
// Inline mapping for various internal formats to format and type
// Most common formats (RGBA8, RGBA, RGBA16F, R8, RGBA32F) are handled first
// with early return for maximum performance on the hot path.
// ============================================================================
void internal_convert(GLenum* internal_format, GLenum* type, GLenum* format) {
    if (format && *format == GL_BGRA) *format = GL_RGBA;

    // Fast path: most common formats first
    switch (*internal_format) {
    // --- Most common formats (hot path) ---
    case GL_RGBA8:
    case GL_RGBA:
        if (type) *type = GL_UNSIGNED_BYTE;
        if (format) *format = GL_RGBA;
        return;
    case GL_RGBA16F:
        if (type) *type = GL_HALF_FLOAT;
        return;
    case GL_R8:
        if (format) *format = GL_RED;
        if (type) *type = GL_UNSIGNED_BYTE;
        return;
    case GL_RGBA32F:
    case GL_RGB32F:
        if (type) *type = GL_FLOAT;
        return;
    // --- Less common formats ---
    case GL_DEPTH_COMPONENT16:
        if (type) *type = GL_UNSIGNED_SHORT;
        break;
    case GL_DEPTH_COMPONENT24:
        if (type) *type = GL_UNSIGNED_INT;
        break;
    case GL_DEPTH_COMPONENT32:
        *internal_format = GL_DEPTH_COMPONENT;
        if (type) *type = GL_UNSIGNED_INT;
        break;
    case GL_DEPTH_COMPONENT32F:
        if (type) *type = GL_FLOAT;
        break;
    case GL_DEPTH_COMPONENT:
        LOG_D("Find GL_DEPTH_COMPONENT: internalFormat: %s, format: %s, type: %s", glEnumToString(*internal_format),
              glEnumToString(*format), glEnumToString(*type));
        if (type) {
            *internal_format = GL_DEPTH_COMPONENT;
            *type = GL_UNSIGNED_INT;
        }
        break;
    case GL_DEPTH_STENCIL:
        *internal_format = GL_DEPTH32F_STENCIL8;
        if (type) *type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
        break;
    case GL_RGB10_A2:
        if (type) *type = GL_UNSIGNED_INT_2_10_10_10_REV;
        break;
    case GL_RGB5_A1:
        if (type) *type = GL_UNSIGNED_SHORT_5_5_5_1;
        break;
    case GL_COMPRESSED_RED_RGTC1:
    case GL_COMPRESSED_RG_RGTC2:
        LOG_E("GL_COMPRESSED_RED_RGTC1 or GL_COMPRESSED_RG_RGTC2 is not supported!");
        break;
    case GL_SRGB8:
        if (type) *type = GL_UNSIGNED_BYTE;
        break;
    case GL_RGB9_E5:
        if (type) *type = GL_UNSIGNED_INT_5_9_9_9_REV;
        break;
    case GL_R11F_G11F_B10F:
        if (type) *type = GL_UNSIGNED_INT_10F_11F_11F_REV;
        if (format) *format = GL_RGB;
        break;
    case GL_RGBA32UI:
    case GL_RGB32UI:
        if (type) *type = GL_UNSIGNED_INT;
        break;
    case GL_RGBA32I:
    case GL_RGB32I:
        if (type) *type = GL_INT;
        break;
    case GL_RGBA16: {
        if (g_gles_caps.GL_EXT_texture_norm16) {
            if (type) *type = GL_UNSIGNED_SHORT;
        } else {
            *internal_format = GL_RGBA16F;
            if (type) *type = GL_FLOAT;
        }
        break;
    }
    case GL_R16:
        *internal_format = GL_R16F;
        if (type) *type = GL_FLOAT;
        break;
    case GL_RGB16:
        *internal_format = GL_RGB16F;
        if (type) *type = GL_HALF_FLOAT;
        if (format) *format = GL_RGB;
        break;
    case GL_RGB16F:
        if (type) *type = GL_HALF_FLOAT;
        if (format) *format = GL_RGB;
        break;
    case GL_RG16:
        *internal_format = GL_RG16F;
        if (type) *type = GL_HALF_FLOAT;
        if (format) *format = GL_RG;
        break;
    case GL_R8_SNORM:
        if (format) *format = GL_RED;
        if (type) *type = GL_BYTE;
        break;
    case GL_R16F:
        if (format) *format = GL_RED;
        if (type) *type = GL_HALF_FLOAT;
        break;
    case GL_RED:
        if (type) {
            switch (*type) {
            case GL_UNSIGNED_BYTE:
                *internal_format = GL_R8;
                if (format) *format = GL_RED;
                break;
            case GL_BYTE:
                *internal_format = GL_R8_SNORM;
                if (format) *format = GL_RED;
                break;
            case GL_HALF_FLOAT:
                *internal_format = GL_R16F;
                if (format) *format = GL_RED;
                break;
            case GL_FLOAT:
                *internal_format = GL_R32F;
                if (format) *format = GL_RED;
                break;
            default:
                LOG_E("Unsupported type for GL_RED: %s", glEnumToString(*type));
                if (type) *type = GL_UNSIGNED_BYTE;
                *internal_format = GL_R8;
                if (format) *format = GL_RED;
                break;
            }
        }
        break;
    case GL_R8UI:
        if (format) *format = GL_RED_INTEGER;
        if (type) *type = GL_UNSIGNED_BYTE;
        break;
    case GL_R8I:
        if (format) *format = GL_RED_INTEGER;
        if (type) *type = GL_BYTE;
        break;
    case GL_R16UI:
        if (format) *format = GL_RED_INTEGER;
        if (type) *type = GL_UNSIGNED_SHORT;
        break;
    case GL_R16I:
        if (format) *format = GL_RED_INTEGER;
        if (type) *type = GL_SHORT;
        break;
    case GL_R32UI:
        if (format) *format = GL_RED_INTEGER;
        if (type) *type = GL_UNSIGNED_INT;
        break;
    case GL_R32I:
        if (format) *format = GL_RED_INTEGER;
        if (type) *type = GL_INT;
        break;
    case GL_RG8:
        if (format) *format = GL_RG;
        if (type) *type = GL_UNSIGNED_BYTE;
        break;
    case GL_RG8_SNORM:
        if (format) *format = GL_RG;
        if (type) *type = GL_BYTE;
        break;
    case GL_RG16F:
        if (format) *format = GL_RG;
        if (type) *type = GL_HALF_FLOAT;
        break;
    case GL_RG32F:
        if (format) *format = GL_RG;
        if (type) *type = GL_FLOAT;
        break;
    case GL_RG8UI:
        if (format) *format = GL_RG_INTEGER;
        if (type) *type = GL_UNSIGNED_BYTE;
        break;
    case GL_RG8I:
        if (format) *format = GL_RG_INTEGER;
        if (type) *type = GL_BYTE;
        break;
    case GL_RG16UI:
        if (format) *format = GL_RG_INTEGER;
        if (type) *type = GL_UNSIGNED_SHORT;
        break;
    case GL_RG16I:
        if (format) *format = GL_RG_INTEGER;
        if (type) *type = GL_SHORT;
        break;
    case GL_RG32UI:
        if (format) *format = GL_RG_INTEGER;
        if (type) *type = GL_UNSIGNED_INT;
        break;
    case GL_RG32I:
        if (format) *format = GL_RG_INTEGER;
        if (type) *type = GL_INT;
        break;
    case GL_R32F:
        if (format) *format = GL_RED;
        if (type) *type = GL_FLOAT;
        break;
    case GL_RGBA8_SNORM:
        if (format) *format = GL_RGBA;
        if (type) *type = GL_BYTE;
        break;
    default:
        // fallback handling for GL_RGB8, GL_RGBA16_SNORM etc.
        if (*internal_format == GL_RGB8) {
            if (type && *type != GL_UNSIGNED_BYTE) *type = GL_UNSIGNED_BYTE;
            if (format) *format = GL_RGB;
        } else if (*internal_format == GL_RGBA16_SNORM) {
            if (type && *type != GL_SHORT) *type = GL_SHORT;
        }
        break;
    }
}

// ============================================================================
// Internal helpers: depth format detection, binding query
// ============================================================================

static int is_depth_format(GLenum format) {
    switch (format) {
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
        return 1;
    default:
        return 0;
    }
}

static GLenum get_binding_for_target(GLenum target) {
    switch (target) {
    case GL_TEXTURE_2D:
        return GL_TEXTURE_BINDING_2D;
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return GL_TEXTURE_BINDING_CUBE_MAP;
    default:
        return 0;
    }
}

// ============================================================================
// Convenience macro: get texture object from bound target
// ============================================================================

#define GET_TEXTURE_OBJECT(target)                                                                                     \
    unsigned __currentUnitIndex = GetCurrentTextureUnitIndex();                                                        \
    auto& __currentUnit = GetTextureUnit(__currentUnitIndex);                                                          \
    auto targetR = ConvertGLEnumToTextureTarget(target);                                                               \
    if (targetR == TextureTarget::UNKNWON) {                                                                           \
        LOG_E("%s: Unknown texture target: %s", __func__, glEnumToString(target))                                      \
        return;                                                                                                        \
    }                                                                                                                  \
    auto& __bindingSlot = __currentUnit.GetBindingSlot(targetR);                                                       \
    auto tex = __bindingSlot.GetBoundObject()

// ============================================================================
// glBindTexture (native, with 1D→2D mapping for legacy targets)
// ============================================================================

void glBindTexture(GLenum target, GLuint texture) {
    LOG()
    LOG_D("glBindTexture(%s, %d)", glEnumToString(target), texture)
    INIT_CHECK_GL_ERROR

    if (hardware && gl_state && hardware->emulate_texture_buffer && target == GL_TEXTURE_BUFFER) {
        GLES.glActiveTexture(GL_TEXTURE0 + 15);
        GLES.glBindTexture(GL_TEXTURE_2D, texture);
        g_tracked_tex2d_binding[15] = texture;
        GLES.glActiveTexture(GL_TEXTURE0 + gl_state->current_tex_unit);
    } else {
        GLES.glBindTexture(target, texture);
    }
    CHECK_GL_ERROR_NO_INIT

    const int currentUnitIndex = GetCurrentTextureUnitIndex();

    // Track GL_TEXTURE_2D binding per-unit to avoid glGetIntegerv GPU queries
    if (target == GL_TEXTURE_2D) {
        g_tracked_tex2d_binding[currentUnitIndex] = texture;
    }

    auto& currentUnit = GetTextureUnit(currentUnitIndex);
    auto targetR = ConvertGLEnumToTextureTarget(target);
    if (targetR == TextureTarget::UNKNWON) {
        LOG_E("glBindTexture: Unknown texture target: %s", glEnumToString(target));
        return;
    }
    auto& bindingSlot = currentUnit.GetBindingSlot(targetR);
    auto textureObject = GetOrCreateTextureObject(texture);
    if (!textureObject) {
        LOG_W("glBindTexture: Failed to get or create texture object for ID %d, it may be not tracked", texture);
        return;
    }
    bindingSlot.Bind(textureObject);
    textureObject->target = targetR;
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    LOG()
    INIT_CHECK_GL_ERROR
    GLES.glDeleteTextures(n, textures);
    CHECK_GL_ERROR_NO_INIT

    for (GLsizei i = 0; i < n; ++i) {
        MarkTextureObjectForDeletion(textures[i]);
    }
}

void glActiveTexture(GLenum texture) {
    LOG()
    LOG_D("glActiveTexture, texture = %s", glEnumToString(texture))
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + MAX_TEXTURE_IMAGE_UNITS) {
        LOG_E("Invalid texture enum: %s", glEnumToString(texture))
        return;
    }

    set_gl_state_current_tex_unit(texture - GL_TEXTURE0);
    GLES.glActiveTexture(texture);
    ActivateTextureUnit(texture - GL_TEXTURE0);
    CHECK_GL_ERROR
}

// ============================================================================
// Native texture image specification (ES 3.2)
// ============================================================================

// --- glTexImage2D (native, with format conversion) ---
void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid* pixels) {
    LOG()
    GLenum transfer_format = format;

    LOG_D("mg_glTexImage2D,target: %s,level: %d,internalFormat: %s->%s,width: "
          "%d,height: %d,border: %d,format: %s,type: %s, pixels: 0x%x",
          glEnumToString(target), level, glEnumToString(internalFormat), glEnumToString(internalFormat), width, height,
          border, glEnumToString(format), glEnumToString(type), pixels)
    internal_convert(reinterpret_cast<GLenum*>(&internalFormat), &type, &format);

    LOG_D("GLES.glTexImage2D,target: %s,level: %d,internalFormat: %s->%s,width: "
          "%d,height: %d,border: %d,format: %s,type: %s, pixels: 0x%x",
          glEnumToString(target), level, glEnumToString(internalFormat), glEnumToString(internalFormat), width, height,
          border, glEnumToString(format), glEnumToString(type), pixels)
    GLenum rtarget = map_tex_target(target);
    if (rtarget == GL_PROXY_TEXTURE_2D) {
        int max1 = 4096;
        GLES.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max1);
        set_gl_state_proxy_width(((width << level) > max1) ? 0 : width);
        set_gl_state_proxy_height(((height << level) > max1) ? 0 : height);
        set_gl_state_proxy_intformat(internalFormat);
        return;
    }

    GET_TEXTURE_OBJECT(target);
    tex->target = ConvertGLEnumToTextureTarget(target);
    tex->internal_format = internalFormat;
    tex->width = width;
    tex->height = height;
    tex->depth = 1;
    tex->swizzle_param[0] = GL_RED;
    tex->swizzle_param[1] = GL_GREEN;
    tex->swizzle_param[2] = GL_BLUE;
    tex->swizzle_param[3] = GL_ALPHA;

    if (transfer_format == GL_BGRA && tex->format != transfer_format && internalFormat == GL_RGBA8 && width <= 128 &&
        height <= 128) { // xaero has 64x64 tiles...hack here
        LOG_D("Detected GL_BGRA format @ tex = %d, do swizzle", tex->texture)
        if (tex->swizzle_param[0] == 0) { // assert this as never called glTexParameteri(...,
                                          // GL_TEXTURE_SWIZZLE_R, ...)
            tex->swizzle_param[0] = GL_RED;
            tex->swizzle_param[1] = GL_GREEN;
            tex->swizzle_param[2] = GL_BLUE;
            tex->swizzle_param[3] = GL_ALPHA;
        }

        GLint r = tex->swizzle_param[0];
        GLint g = tex->swizzle_param[1];
        GLint b = tex->swizzle_param[2];
        GLint a = tex->swizzle_param[3];
        tex->swizzle_param[0] = g;
        tex->swizzle_param[1] = b;
        tex->swizzle_param[2] = a;
        tex->swizzle_param[3] = r;
        tex->format = transfer_format;

        GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, tex->swizzle_param[0]);
        GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, tex->swizzle_param[1]);
        GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, tex->swizzle_param[2]);
        GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, tex->swizzle_param[3]);
        CHECK_GL_ERROR
    }

    tex->format = format;

    GLES.glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);

    CHECK_GL_ERROR
}

// --- glTexImage3D (native, ES 3.2 supports it) ---
void glTexImage3D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type, const GLvoid* pixels) {
    LOG()
    LOG_D("glTexImage3D, target: 0x%x, level: %d, internalFormat: 0x%x, width: "
          "0x%x, height: %d, depth: %d, border: %d, format: 0x%x, type: %d",
          target, level, internalFormat, width, height, depth, border, format, type)

    internal_convert(reinterpret_cast<GLenum*>(&internalFormat), &type, &format);
    GLenum rtarget = map_tex_target(target);
    if (rtarget == GL_PROXY_TEXTURE_3D) {
        int max1 = 4096;
        GLES.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max1);
        set_gl_state_proxy_width(((width << level) > max1) ? 0 : width);
        set_gl_state_proxy_height(((height << level) > max1) ? 0 : height);
        set_gl_state_proxy_intformat(internalFormat);
        return;
    }

    GLES.glTexImage3D(target, level, internalFormat, width, height, depth, border, format, type, pixels);

    GET_TEXTURE_OBJECT(target);
    tex->target = ConvertGLEnumToTextureTarget(target);
    tex->internal_format = internalFormat;
    tex->width = width;
    tex->height = height;
    tex->depth = depth;
    tex->swizzle_param[0] = GL_RED;
    tex->swizzle_param[1] = GL_GREEN;
    tex->swizzle_param[2] = GL_BLUE;
    tex->swizzle_param[3] = GL_ALPHA;

    CHECK_GL_ERROR
}

// --- glTexSubImage2D (native, with format conversion) ---
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const void* pixels) {
    LOG()

    LOG_D("glTexSubImage2D, target = %s, level = %d, xoffset = %d, yoffset = %d, "
          "width = %d, height = %d, format = %s, type = %s, pixels = 0x%x",
          glEnumToString(target), level, xoffset, yoffset, width, height, glEnumToString(format), glEnumToString(type),
          pixels)

    if (format == GL_BGRA && (type == GL_UNSIGNED_INT_8_8_8_8 || type == GL_UNSIGNED_INT_8_8_8_8_REV)) {
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_R,  GL_BLUE);
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_B,  GL_RED);

        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    }

    GLES.glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);

    CHECK_GL_ERROR
}

// --- glTexSubImage3D (native) ---
void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width,
                     GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels) {
    LOG()
    LOG_D("glTexSubImage3D, target: %s, level: %d, xoffset: %d, yoffset: %d, zoffset: %d, "
          "width: %d, height: %d, depth: %d, format: %s, type: %s",
          glEnumToString(target), level, xoffset, yoffset, zoffset, width, height, depth, glEnumToString(format),
          glEnumToString(type))

    GLES.glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
    CHECK_GL_ERROR
}

// --- glTexStorage2D (native) ---
void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height) {
    LOG()
    LOG_D("glTexStorage2D, target: %d, levels: %d, internalFormat: %d, width: "
          "%d, height: %d",
          target, levels, internalFormat, width, height)

    internal_convert(&internalFormat, nullptr, nullptr);
    GLES.glTexStorage2D(target, levels, internalFormat, width, height);

    GET_TEXTURE_OBJECT(target);
    tex->target = ConvertGLEnumToTextureTarget(target);
    tex->internal_format = internalFormat;
    tex->width = width;
    tex->height = height;
    tex->depth = 1;
    tex->swizzle_param[0] = GL_RED;
    tex->swizzle_param[1] = GL_GREEN;
    tex->swizzle_param[2] = GL_BLUE;
    tex->swizzle_param[3] = GL_ALPHA;

    GLenum ERR = GLES.glGetError();
    if (ERR != GL_NO_ERROR) LOG_E("glTexStorage2D ERROR: %d", ERR)
}

// --- glTexStorage3D (native) ---
void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height,
                    GLsizei depth) {
    LOG()
    LOG_D("glTexStorage3D, target: %d, levels: %d, internalFormat: %d, width: "
          "%d, height: %d, depth: %d",
          target, levels, internalFormat, width, height, depth)

    internal_convert(&internalFormat, nullptr, nullptr);

    GLES.glTexStorage3D(target, levels, internalFormat, width, height, depth);

    GET_TEXTURE_OBJECT(target);
    tex->target = ConvertGLEnumToTextureTarget(target);
    tex->internal_format = internalFormat;
    tex->width = width;
    tex->height = height;
    tex->depth = depth;
    tex->swizzle_param[0] = GL_RED;
    tex->swizzle_param[1] = GL_GREEN;
    tex->swizzle_param[2] = GL_BLUE;
    tex->swizzle_param[3] = GL_ALPHA;

    CHECK_GL_ERROR
}

// ============================================================================
// Native compressed texture functions (ES 3.2)
// ============================================================================

// --- glCompressedTexImage2D (native) ---
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                            GLint border, GLsizei imageSize, const void* data) {
    LOG()
    LOG_D("glCompressedTexImage2D, target: %s, level: %d, internalformat: %s, width: %d, height: %d, "
          "border: %d, imageSize: %d",
          glEnumToString(target), level, glEnumToString(internalformat), width, height, border, imageSize)

    GLES.glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);

    GET_TEXTURE_OBJECT(target);
    tex->target = ConvertGLEnumToTextureTarget(target);
    tex->internal_format = internalformat;
    tex->width = width;
    tex->height = height;
    tex->depth = 1;
    tex->swizzle_param[0] = GL_RED;
    tex->swizzle_param[1] = GL_GREEN;
    tex->swizzle_param[2] = GL_BLUE;
    tex->swizzle_param[3] = GL_ALPHA;

    CHECK_GL_ERROR
}

// --- glCompressedTexImage3D (native) ---
void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                            GLsizei depth, GLint border, GLsizei imageSize, const void* data) {
    LOG()
    LOG_D("glCompressedTexImage3D, target: %s, level: %d, internalformat: %s, width: %d, height: %d, "
          "depth: %d, border: %d, imageSize: %d",
          glEnumToString(target), level, glEnumToString(internalformat), width, height, depth, border, imageSize)

    GLES.glCompressedTexImage3D(target, level, internalformat, width, height, depth, border, imageSize, data);

    GET_TEXTURE_OBJECT(target);
    tex->target = ConvertGLEnumToTextureTarget(target);
    tex->internal_format = internalformat;
    tex->width = width;
    tex->height = height;
    tex->depth = depth;
    tex->swizzle_param[0] = GL_RED;
    tex->swizzle_param[1] = GL_GREEN;
    tex->swizzle_param[2] = GL_BLUE;
    tex->swizzle_param[3] = GL_ALPHA;

    CHECK_GL_ERROR
}

// --- glCompressedTexSubImage2D (native) ---
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                               GLsizei height, GLenum format, GLsizei imageSize, const void* data) {
    LOG()
    LOG_D("glCompressedTexSubImage2D, target: %s, level: %d, xoffset: %d, yoffset: %d, "
          "width: %d, height: %d, format: %s, imageSize: %d",
          glEnumToString(target), level, xoffset, yoffset, width, height, glEnumToString(format), imageSize)

    GLES.glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
    CHECK_GL_ERROR
}

// --- glCompressedTexSubImage3D (native) ---
void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
                               GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize,
                               const void* data) {
    LOG()
    LOG_D("glCompressedTexSubImage3D, target: %s, level: %d, xoffset: %d, yoffset: %d, zoffset: %d, "
          "width: %d, height: %d, depth: %d, format: %s, imageSize: %d",
          glEnumToString(target), level, xoffset, yoffset, zoffset, width, height, depth, glEnumToString(format),
          imageSize)

    GLES.glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize,
                                   data);
    CHECK_GL_ERROR
}

// ============================================================================
// Native copy texture functions (ES 3.2)
// ============================================================================

// --- glCopyTexImage2D (native) ---
void glCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width,
                      GLsizei height, GLint border) {
    LOG()

    INIT_CHECK_GL_ERROR

    GLint realInternalFormat;
    GLES.glGetTexLevelParameteriv(target, level, GL_TEXTURE_INTERNAL_FORMAT, &realInternalFormat);
    internalFormat = (GLenum)realInternalFormat;

    LOG_D("glCopyTexImage2D, target: %d, level: %d, internalFormat: %d, x: %d, "
          "y: %d, width: %d, height: %d, border: %d",
          target, level, internalFormat, x, y, width, height, border)

    if (is_depth_format(internalFormat)) {
        GLenum format = GL_DEPTH_COMPONENT;
        GLenum type = GL_UNSIGNED_INT;
        internal_convert(&internalFormat, &type, &format);
        GLES.glTexImage2D(target, level, (GLint)internalFormat, width, height, border, format, type, nullptr);
        CHECK_GL_ERROR_NO_INIT
        GLint prevDrawFBO;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
        CHECK_GL_ERROR_NO_INIT
        GLuint tempDrawFBO = acquireTempFBO();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempDrawFBO);
        CHECK_GL_ERROR_NO_INIT
        GLint currentTex;
        glGetIntegerv(get_binding_for_target(target), &currentTex);
        CHECK_GL_ERROR_NO_INIT
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, currentTex, level);
        CHECK_GL_ERROR_NO_INIT

        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) [[unlikely]] {
            CHECK_GL_ERROR_NO_INIT
            releaseTempFBO(tempDrawFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
            CHECK_GL_ERROR_NO_INIT
            return;
        }
        CHECK_GL_ERROR_NO_INIT

        GLES.glBlitFramebuffer(x, y, x + width, y + height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        CHECK_GL_ERROR_NO_INIT

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        releaseTempFBO(tempDrawFBO);
        CHECK_GL_ERROR_NO_INIT
    } else {
        GLES.glCopyTexImage2D(target, level, internalFormat, x, y, width, height, border);
        CHECK_GL_ERROR_NO_INIT
    }

    GET_TEXTURE_OBJECT(target);
    tex->target = ConvertGLEnumToTextureTarget(target);
    tex->internal_format = internalFormat;
    tex->width = width;
    tex->height = height;
    tex->depth = 1;
    tex->swizzle_param[0] = GL_RED;
    tex->swizzle_param[1] = GL_GREEN;
    tex->swizzle_param[2] = GL_BLUE;
    tex->swizzle_param[3] = GL_ALPHA;

    CHECK_GL_ERROR_NO_INIT
}

// --- glCopyTexSubImage2D (native) ---
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) {
    LOG()
    GLint internalFormat;
    GLES.glGetTexLevelParameteriv(target, level, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);

    LOG_D("glCopyTexSubImage2D, target: %s, level: %d, xoffset: %d, yoffset: %d, "
          "x: %d, y: %d, width: %d, height: %d",
          glEnumToString(target), level, xoffset, yoffset, x, y, width, height)

    if (is_depth_format((GLenum)internalFormat)) {
        GLint prevReadFBO, prevDrawFBO;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

        GLuint tempDrawFBO = acquireTempFBO();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempDrawFBO);

        GLint currentTex;
        glGetIntegerv(get_binding_for_target(target), &currentTex);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, currentTex, level);

        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) [[unlikely]] {
            releaseTempFBO(tempDrawFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
            return;
        }

        GLES.glBlitFramebuffer(x, y, x + width, y + height, xoffset, yoffset, xoffset + width, yoffset + height,
                               GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        releaseTempFBO(tempDrawFBO);
    } else {
        GLES.glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
    }

    CHECK_GL_ERROR
}

// --- glCopyTexSubImage3D (native) ---
void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y,
                         GLsizei width, GLsizei height) {
    LOG()
    LOG_D("glCopyTexSubImage3D, target: %s, level: %d, xoffset: %d, yoffset: %d, zoffset: %d, "
          "x: %d, y: %d, width: %d, height: %d",
          glEnumToString(target), level, xoffset, yoffset, zoffset, x, y, width, height)

    GLES.glCopyTexSubImage3D(target, level, xoffset, yoffset, zoffset, x, y, width, height);
    CHECK_GL_ERROR
}

// ============================================================================
// Native mipmap generation (ES 3.2)
// ============================================================================

// --- glGenerateMipmap (native) ---
void glGenerateMipmap(GLenum target) {
    LOG()
    LOG_D("glGenerateMipmap, target: %s", glEnumToString(target))
    GLES.glGenerateMipmap(target);
    CHECK_GL_ERROR
}

// ============================================================================
// Native texture parameter functions (ES 3.2)
// ============================================================================

// --- glTexParameterf (native) ---
void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    LOG()
    pname = pname_convert(pname);
    LOG_D("glTexParameterf, target: %d, pname: %d, param: %f", target, pname, param)

    if (pname == GL_TEXTURE_LOD_BIAS_QCOM && !g_gles_caps.GL_QCOM_texture_lod_bias) {
        LOG_D("Does not support GL_QCOM_texture_lod_bias, skipped!")
        return;
    }

    GLES.glTexParameterf(target, pname, param);
    CHECK_GL_ERROR
}

// --- glTexParameteri (native) ---
void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    LOG()
    pname = pname_convert(pname);
    LOG_D("glTexParameteri, pname: 0x%x", pname)

    if (pname == GL_TEXTURE_LOD_BIAS_QCOM && !g_gles_caps.GL_QCOM_texture_lod_bias) {
        LOG_D("Does not support GL_QCOM_texture_lod_bias, skipped!")
        return;
    }

    GLES.glTexParameteri(target, pname, param);
    CHECK_GL_ERROR
}

// --- glTexParameteriv (native) ---
void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    LOG()
    LOG_D("glTexParameteriv, target: %s, pname: %s", glEnumToString(target), glEnumToString(pname))

    if (pname == GL_TEXTURE_SWIZZLE_RGBA) {
        LOG_D("find GL_TEXTURE_SWIZZLE_RGBA, now use glTexParameteri")
        if (params) {
            // deferred those call to draw call?
            GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, params[0]);
            GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, params[1]);
            GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, params[2]);
            GLES.glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, params[3]);

            // save states for now
            GET_TEXTURE_OBJECT(target);
            tex->swizzle_param[0] = params[0];
            tex->swizzle_param[1] = params[1];
            tex->swizzle_param[2] = params[2];
            tex->swizzle_param[3] = params[3];
        } else {
            LOG_E("glTexParameteriv: params is nullptr for GL_TEXTURE_SWIZZLE_RGBA")
        }
    } else {
        GLES.glTexParameteriv(target, pname, params);
    }

    CHECK_GL_ERROR
}

// --- glTexParameterfv (native) ---
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    LOG()
    LOG_D("glTexParameterfv, target: %s, pname: %s", glEnumToString(target), glEnumToString(pname))
    GLES.glTexParameterfv(target, pname, params);
    CHECK_GL_ERROR
}

// ============================================================================
// Native texture query functions (ES 3.2)
// ============================================================================

// --- glGetTexParameteriv (native) ---
void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    LOG()
    LOG_D("glGetTexParameteriv, target: %s, pname: %s", glEnumToString(target), glEnumToString(pname))
    GLES.glGetTexParameteriv(target, pname, params);
    CHECK_GL_ERROR
}

// --- glGetTexParameterfv (native) ---
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
    LOG()
    LOG_D("glGetTexParameterfv, target: %s, pname: %s", glEnumToString(target), glEnumToString(pname))
    GLES.glGetTexParameterfv(target, pname, params);
    CHECK_GL_ERROR
}

// --- glGetTexLevelParameterfv (native) ---
void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat* params) {
    LOG()
    LOG_D("glGetTexLevelParameterfv,target: %d, level: %d, pname: %d", target, level, pname)
    if (gl_state) {
        GLenum rtarget = map_tex_target(target);
        if (rtarget == GL_PROXY_TEXTURE_2D) {
            switch (pname) {
            case GL_TEXTURE_WIDTH:
                (*params) = (float)nlevel(gl_state->proxy_width, level);
                return;
            case GL_TEXTURE_HEIGHT:
                (*params) = (float)nlevel(gl_state->proxy_height, level);
                return;
            case GL_TEXTURE_INTERNAL_FORMAT:
                (*params) = (float)gl_state->proxy_intformat;
                return;
            default:
                return;
            }
        }
    }
    GLES.glGetTexLevelParameterfv(target, level, pname, params);
    CHECK_GL_ERROR
}

// --- glGetTexLevelParameteriv (native) ---
void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    LOG()
    LOG_D("glGetTexLevelParameteriv,target: %s, level: %d, pname: %s", glEnumToString(target), level,
          glEnumToString(pname))
    if (gl_state) {
        GLenum rtarget = map_tex_target(target);
        if (rtarget == GL_PROXY_TEXTURE_2D) {
            switch (pname) {
            case GL_TEXTURE_WIDTH:
                (*params) = nlevel(gl_state->proxy_width, level);
                return;
            case GL_TEXTURE_HEIGHT:
                (*params) = nlevel(gl_state->proxy_height, level);
                return;
            case GL_TEXTURE_INTERNAL_FORMAT:
                (*params) = (GLint)gl_state->proxy_intformat;
                return;
            default:
                return;
            }
        }
    }
    LOG_D("es.glGetTexLevelParameteriv,target: %s, level: %d, pname: %s", glEnumToString(target), level,
          glEnumToString(pname))
    GLES.glGetTexLevelParameteriv(target, level, pname, params);
    CHECK_GL_ERROR
}

// ============================================================================
// Renderbuffer functions (native)
// ============================================================================

void glRenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width, GLsizei height) {
    LOG()

    INIT_CHECK_GL_ERROR_FORCE

    LOG_D("glRenderbufferStorage, target: %s, internalFormat: %s, width: %d, "
          "height: %d",
          glEnumToString(target), glEnumToString(internalFormat), width, height)

    GLES.glRenderbufferStorage(target, internalFormat, width, height);

    CHECK_GL_ERROR_NO_INIT
}

void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width,
                                      GLsizei height) {
    LOG()

    INIT_CHECK_GL_ERROR_FORCE

    LOG_D("glRenderbufferStorageMultisample, target: %d, samples: %d, "
          "internalFormat: %d, width: %d, height: %d",
          target, samples, internalFormat, width, height)

    GLES.glRenderbufferStorageMultisample(target, samples, internalFormat, width, height);

    CHECK_GL_ERROR_NO_INIT
}

// ============================================================================
// Other texture functions (keep existing logic)
// ============================================================================

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels) {
    LOG()
    LOG_D("glGetTexImage, target: 0x%x, level: %d, format: 0x%x, type: 0x%x, pixels: 0x%x", target, level, format, type,
          pixels)
    GLint prevDrawFBO;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
    GLint prevReadFBO;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);

    GLuint tempFBO = acquireTempFBO();
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);

    GLint textureId = 0;
    GLenum textureBindingTarget;
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
        textureBindingTarget = GL_TEXTURE_BINDING_CUBE_MAP;
    } else if (target == GL_TEXTURE_2D) {
        textureBindingTarget = GL_TEXTURE_BINDING_2D;
    } else {
        LOG_E("glGetTexImage: Unsupported or complex target: 0x%x", target)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        releaseTempFBO(tempFBO);
        return;
    }
    glGetIntegerv(textureBindingTarget, &textureId);

    if (textureId == 0) {
        LOG_E("glGetTexImage: No texture bound to the specified target.")
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        releaseTempFBO(tempFBO);
        return;
    }

    GLint width = 0, height = 0;
    glGetTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(target, level, GL_TEXTURE_HEIGHT, &height);

    if (width == 0 || height == 0) {
        LOG_E("glGetTexImage: Texture level %d has zero width or height.", level)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        releaseTempFBO(tempFBO);
        return;
    }

    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, textureId, level);
    } else if (target == GL_TEXTURE_2D) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, level);
    }

    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) [[unlikely]] {
        LOG_E("glGetTexImage: Failed to create complete framebuffer. Status: 0x%x", fboStatus)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        releaseTempFBO(tempFBO);
        return;
    }

    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glReadPixels(0, 0, width, height, format, type, pixels);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    releaseTempFBO(tempFBO);
}

#if GLOBAL_DEBUG || DEBUG
#include "../config/config.h"
#include <fstream>
#endif

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels) {
    LOG()
    LOG_D("glReadPixels, x=%d, y=%d, width=%d, height=%d, format=0x%x, "
          "type=0x%x, pixels=0x%x",
          x, y, width, height, format, type, pixels)

    static int count = 0;
    GLenum prevFormat = format;

    if (format == GL_BGRA && type == GL_UNSIGNED_INT_8_8_8_8) {
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    }
    LOG_D("glReadPixels converted, x=%d, y=%d, width=%d, height=%d, format=0x%x, "
          "type=0x%x, pixels=0x%x",
          x, y, width, height, format, type, pixels)
    GLES.glReadPixels(x, y, width, height, format, type, pixels);

#if GLOBAL_DEBUG || DEBUG
    if (prevFormat == GL_BGRA && type == GL_UNSIGNED_BYTE) {
        std::vector<uint8_t> px(width * height * sizeof(uint8_t) * 4, 0);
        GLES.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        GLES.glReadPixels(x, y, width, height, format, type, px.data());

        std::fstream fs(std::string(concatenate(mg_directory_path, "/readpixels/")) + std::to_string(count++) + ".bin",
                        std::ios::out | std::ios::binary | std::ios::trunc);
        fs.write((const char*)px.data(), px.size());
        fs.close();
    }
#endif
    CHECK_GL_ERROR
}

void glClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void* data) {
    LOG()
    LOG_D("glClearTexImage, texture: %d, level: %d, format: %d, type: %d", texture, level, format, type)
    INIT_CHECK_GL_ERROR_FORCE
    GLuint fbo, prevDrawFBO, prevReadFBO;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (int*)&prevDrawFBO);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, (int*)&prevReadFBO);
    fbo = acquireTempFBO();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    CHECK_GL_ERROR_NO_INIT

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, level);

    CHECK_GL_ERROR_NO_INIT
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) [[unlikely]] {
        LOG_D("  -> exit")
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
        releaseTempFBO(fbo);
        CHECK_GL_ERROR_NO_INIT
        return;
    }

    GLES.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    CHECK_GL_ERROR_NO_INIT

    if (data != nullptr) {
        if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
            auto* byteData = static_cast<const GLubyte*>(data);
            GLES.glClearColor((float)byteData[0] / 255.0f, (float)byteData[1] / 255.0f, (float)byteData[2] / 255.0f,
                              (float)byteData[3] / 255.0f);
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
            auto* byteData = static_cast<const GLubyte*>(data);
            GLES.glClearColor((float)byteData[0] / 255.0f, (float)byteData[1] / 255.0f, (float)byteData[2] / 255.0f,
                              1.0f);
        } else if (format == GL_RGBA && type == GL_FLOAT) {
            auto* floatData = static_cast<const GLfloat*>(data);
            GLES.glClearColor(floatData[0], floatData[1], floatData[2], floatData[3]);
        } else if (format == GL_RGB && type == GL_FLOAT) {
            auto* floatData = static_cast<const GLfloat*>(data);
            GLES.glClearColor(floatData[0], floatData[1], floatData[2], 1.0f);
        } else if (format == GL_DEPTH_COMPONENT && type == GL_FLOAT) {
            auto* depthData = static_cast<const GLfloat*>(data);
            GLES.glClearDepthf(depthData[0]);
            GLES.glClear(GL_DEPTH_BUFFER_BIT);
        } else if (format == GL_STENCIL_INDEX && type == GL_UNSIGNED_BYTE) {
            auto* stencilData = static_cast<const GLubyte*>(data);
            GLES.glClearStencil(stencilData[0]);
            GLES.glClear(GL_STENCIL_BUFFER_BIT);
        }
    }
    CHECK_GL_ERROR_NO_INIT

    if (format == GL_DEPTH_COMPONENT || format == GL_STENCIL_INDEX) {
        GLES.glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        CHECK_GL_ERROR_NO_INIT
    } else {
        GLES.glClear(GL_COLOR_BUFFER_BIT);
        CHECK_GL_ERROR_NO_INIT
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    releaseTempFBO(fbo);
    CHECK_GL_ERROR_NO_INIT
}

void glPixelStorei(GLenum pname, GLint param) {
    LOG_D("glPixelStorei, pname = %s, param = %d", glEnumToString(pname), param)
    GLES.glPixelStorei(pname, param);
    CHECK_GL_ERROR
}

// glGenerateTextureMipmap is handled in ExtWrappers/DSAWrapper.cpp (DSA emulation)