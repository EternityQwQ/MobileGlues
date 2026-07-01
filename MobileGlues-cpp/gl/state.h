// MobileGlues - gl/state.h
// Unified OpenGL State Manager: encapsulates all CPU-side state tracking
// to avoid GPU round-trips (glGetIntegerv etc.) and provides ID mapping
// for the desktop GL → OpenGL ES 3.2 translation layer.
//
// Architecture principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#pragma once

#include "../includes.h"
#include <GL/gl.h>
#include "glcorearb.h"
#include <GLES3/gl32.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstring>
#include <mutex>

// ============================================================================
// Forward declarations
// ============================================================================

// ============================================================================
// Constants
// ============================================================================

constexpr int MAX_TEXTURE_UNITS = 96;    // GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS minimum
constexpr int MAX_DRAW_BUFFERS = 16;     // GL_MAX_DRAW_BUFFERS minimum
constexpr int MAX_COLOR_ATTACHMENTS = 8;
constexpr int MAX_VERTEX_ATTRIBS = 16;
constexpr int MAX_UNIFORM_BUFFER_BINDINGS = 84;
constexpr int MAX_SHADER_STORAGE_BUFFER_BINDINGS = 64;
constexpr int MAX_ATOMIC_COUNTER_BUFFER_BINDINGS = 8;
constexpr int MAX_IMAGE_UNITS = 16;
constexpr int MAX_TRANSFORM_FEEDBACK_BUFFERS = 4;

// ============================================================================
// Logging
// ============================================================================

#ifdef __ANDROID__
#define MG_LOG(level, fmt, ...) __android_log_print(level, "MobileGlues", fmt, ##__VA_ARGS__)
#define MG_LOG_DEBUG(fmt, ...) MG_LOG(ANDROID_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define MG_LOG_INFO(fmt, ...) MG_LOG(ANDROID_LOG_INFO, fmt, ##__VA_ARGS__)
#define MG_LOG_WARN(fmt, ...) MG_LOG(ANDROID_LOG_WARN, fmt, ##__VA_ARGS__)
#define MG_LOG_ERROR(fmt, ...) MG_LOG(ANDROID_LOG_ERROR, fmt, ##__VA_ARGS__)
#else
#define MG_LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define MG_LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define MG_LOG_WARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define MG_LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

// ============================================================================
// Debug macros
// ============================================================================

#define DEBUG_STATE 0
#if DEBUG_STATE
#define STATE_LOG(fmt, ...) MG_LOG_DEBUG(fmt, ##__VA_ARGS__)
#else
#define STATE_LOG(fmt, ...) ((void)0)
#endif

// ============================================================================
// BufferObject: per-buffer CPU-side metadata (state.h only)
// NOTE: TextureObject is defined in texture.h — do not redefine here.
// ============================================================================

struct MGBufferInfo {
    GLuint id = 0;           // real GLES buffer ID
    GLenum target = 0;
    GLsizeiptr size = 0;
    GLbitfield access = 0;
    void* mappedPtr = nullptr;
    bool isMapped = false;
};

// ============================================================================
// ============================================================================
// GLStateManager: unified state machine
// ============================================================================
// ============================================================================

class GLStateManager {
public:
    static GLStateManager& Instance() {
        static GLStateManager instance;
        return instance;
    }

    // ------------------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------------------

    void Initialize();
    void Shutdown();

    // ------------------------------------------------------------------------
    // Hardware state
    // ------------------------------------------------------------------------

    int esVersion = 32;                    // OpenGL ES version (30 or 32)
    bool emulateTextureBuffer = false;     // Whether TBO emulation is active

    // Backward-compatible aliases for old snake_case field names
    int &es_version = esVersion;
    bool &emulate_texture_buffer = emulateTextureBuffer;

    // ------------------------------------------------------------------------
    // Proxy framebuffer state (for FSR1-style rendering)
    // ------------------------------------------------------------------------

    GLsizei proxyWidth = 0;
    GLsizei proxyHeight = 0;
    GLenum proxyInternalFormat = GL_RGBA8;
    GLuint currentProgram = 0;
    GLint currentTexUnit = 0;
    GLuint currentDrawFBO = 0;

    bool isDrawCall = false;   // guard flag during draw calls

    // ========================================================================
    // Buffer State Management
    // ========================================================================

    struct BufferState {
        // ID mapping: virtual (desktop GL) → real (GLES)
        UnorderedMap<GLuint, GLuint> bufferMap;       // virtual → real
        UnorderedMap<GLuint, GLuint> bufferMapReverse; // real → virtual
        UnorderedMap<GLuint, MGBufferInfo> bufferInfo; // virtual → metadata

        // VAO mapping
        UnorderedMap<GLuint, GLuint> vaoMap;          // virtual → real
        UnorderedMap<GLuint, GLuint> vaoMapReverse;   // real → virtual

        // Current bindings per target
        UnorderedMap<GLenum, GLuint> boundBuffer;     // target → virtual ID

        // Uniform buffer bases
        GLuint uniformBufferBases[MAX_UNIFORM_BUFFER_BINDINGS] = {};
        GLintptr uniformBufferOffsets[MAX_UNIFORM_BUFFER_BINDINGS] = {};
        GLsizeiptr uniformBufferSizes[MAX_UNIFORM_BUFFER_BINDINGS] = {};

        // Shader storage buffer bases
        GLuint shaderStorageBases[MAX_SHADER_STORAGE_BUFFER_BINDINGS] = {};

        // Atomic counter buffer bases
        GLuint atomicCounterBases[MAX_ATOMIC_COUNTER_BUFFER_BINDINGS] = {};

        // Transform feedback buffer bindings
        GLuint transformFeedbackBuffers[MAX_TRANSFORM_FEEDBACK_BUFFERS] = {};

        // Texture buffer objects (for TBO emulation)
        struct TexBufferSlot {
            GLuint buffer = 0;
            GLuint texture = 0;
            GLenum internalFormat = 0;
        };
        UnorderedMap<GLuint, TexBufferSlot> texBuffers; // virtual buffer → {real buffer, tex}

        // Buffer map for glMapBufferRange
        UnorderedMap<GLuint, void*> bufferMaps;         // virtual → mapped pointer

        // Atomic counter buffer data (CPU-side copy for emulation)
        std::vector<GLuint> atomicCounterData;
        GLuint atomicCounterBufferBinding = 0;
        GLsizeiptr atomicCounterBufferSize = 0;
        GLsizeiptr atomicCounterBufferOffset = 0;

        void Reset();
    } buffer;

    // ========================================================================
    // Texture State Management
    // ========================================================================

    struct TextureState {
        // ID mapping: virtual → real GLES texture ID
        // (Per-texture metadata is managed by texture.h's TextureObject class)
        UnorderedMap<GLuint, GLuint> textureMap;       // virtual → real
        UnorderedMap<GLuint, GLuint> textureMapReverse;       // real → virtual

        // Per-unit binding state: unit → target → virtual texture ID
        struct TexUnitState {
            UnorderedMap<GLenum, GLuint> binding;              // target → virtual ID
        };
        TexUnitState texUnits[MAX_TEXTURE_UNITS];

        // Current active texture unit
        GLint activeUnit = 0;

        // Pixel store state
        GLint unpackAlignment = 4;
        GLint unpackRowLength = 0;
        GLint unpackImageHeight = 0;
        GLint unpackSkipPixels = 0;
        GLint unpackSkipRows = 0;
        GLint unpackSkipImages = 0;
        GLint packAlignment = 4;
        GLint packRowLength = 0;
        GLint packImageHeight = 0;
        GLint packSkipPixels = 0;
        GLint packSkipRows = 0;
        GLint packSkipImages = 0;

        void Reset();
    } texture;

    // ========================================================================
    // Framebuffer State Management
    // ========================================================================

    struct FramebufferState {
        // ID mapping: virtual → real
        UnorderedMap<GLuint, GLuint> fboMap;          // virtual → real
        UnorderedMap<GLuint, GLuint> fboMapReverse;   // real → virtual

        // Current bindings
        GLuint drawFBO = 0;    // virtual
        GLuint readFBO = 0;    // virtual

        // Attachment tracking: virtual FBO → attachment → {real FBO, attachment}
        struct AttachmentInfo {
            GLuint fbo = 0;          // real FBO
            GLenum attachment = 0;   // real attachment point
        };
        UnorderedMap<GLuint, UnorderedMap<GLenum, AttachmentInfo>> attachments;

        // Draw buffer state
        GLenum drawBuffers[MAX_DRAW_BUFFERS] = {GL_BACK};
        GLenum readBuffer = GL_BACK;
        int drawBufferCount = 1;

        // Temp FBO pool for format conversion
        struct TempFBO {
            GLuint fbo = 0;
            GLuint texture = 0;
            GLsizei width = 0;
            GLsizei height = 0;
        };
        std::vector<TempFBO> tempFBOs;

        // FBO invalidation state
        bool defaultFboInvalidated = false;

        void Reset();
    } framebuffer;

    // ========================================================================
    // Shader/Program State Management
    // ========================================================================

    struct ShaderState {
        // ID mapping: virtual → real
        UnorderedMap<GLuint, GLuint> shaderMap;       // virtual → real
        UnorderedMap<GLuint, GLuint> shaderMapReverse; // real → virtual
        UnorderedMap<GLuint, GLuint> programMap;       // virtual → real
        UnorderedMap<GLuint, GLuint> programMapReverse; // real → virtual

        // Shader metadata
        struct ShaderInfo {
            GLenum type = 0;
            std::string source;
            bool compiled = false;
        };
        UnorderedMap<GLuint, ShaderInfo> shaderInfo;   // virtual → info

        // Program metadata
        struct ProgramInfo {
            std::vector<GLuint> attachedShaders;  // virtual IDs
            bool linked = false;
            bool validated = false;
        };
        UnorderedMap<GLuint, ProgramInfo> programInfo;  // virtual → info

        // Current program
        GLuint currentProgram = 0;

        void Reset();
    } shader;

    // ========================================================================
    // Sampler State
    // ========================================================================

    struct SamplerState {
        GLuint boundSamplers[MAX_TEXTURE_UNITS] = {};

        void Reset() {
            memset(boundSamplers, 0, sizeof(boundSamplers));
        }
    } sampler;

    // ========================================================================
    // Image Unit State
    // ========================================================================

    struct ImageState {
        struct ImageBinding {
            GLuint texture = 0;
            GLint level = 0;
            GLboolean layered = GL_FALSE;
            GLint layer = 0;
            GLenum access = GL_READ_ONLY;
            GLenum format = GL_RGBA8;
        };
        ImageBinding imageUnits[MAX_IMAGE_UNITS];

        void Reset() {
            memset(imageUnits, 0, sizeof(imageUnits));
        }
    } image;

    // ========================================================================
    // Legacy GL State (for compatibility queries)
    // ========================================================================

    struct LegacyState {
        GLint viewport[4] = {0, 0, 0, 0};
        GLint scissor[4] = {0, 0, 0, 0};
        GLfloat clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        GLfloat clearDepth = 1.0f;
        GLint clearStencil = 0;
        GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        GLboolean depthMask = GL_TRUE;
        GLenum depthFunc = GL_LESS;
        GLenum blendSrcRGB = GL_ONE;
        GLenum blendDstRGB = GL_ZERO;
        GLenum blendSrcAlpha = GL_ONE;
        GLenum blendDstAlpha = GL_ZERO;
        GLenum cullFace = GL_BACK;
        GLenum frontFace = GL_CCW;
        GLfloat lineWidth = 1.0f;
        GLenum polygonMode[2] = {GL_FILL, GL_FILL};
        GLfloat polygonOffsetFactor = 0.0f;
        GLfloat polygonOffsetUnits = 0.0f;
        GLint stencilRef = 0;
        GLuint stencilMask = 0xFFFFFFFF;

        void Reset() {
            memset(this, 0, sizeof(*this));
            viewport[2] = 0; viewport[3] = 0;
            scissor[2] = 0; scissor[3] = 0;
            clearColor[3] = 0.0f;
            clearDepth = 1.0f;
            colorMask[0] = colorMask[1] = colorMask[2] = colorMask[3] = GL_TRUE;
            depthMask = GL_TRUE;
            depthFunc = GL_LESS;
            blendSrcRGB = GL_ONE; blendDstRGB = GL_ZERO;
            blendSrcAlpha = GL_ONE; blendDstAlpha = GL_ZERO;
            cullFace = GL_BACK;
            frontFace = GL_CCW;
            lineWidth = 1.0f;
            polygonMode[0] = GL_FILL; polygonMode[1] = GL_FILL;
            stencilMask = 0xFFFFFFFF;
        }
    } legacy;

    // ========================================================================
    // Vertex Array State
    // ========================================================================

    struct VertexArrayState {
        GLuint currentVAO = 0;  // virtual
        void Reset() { currentVAO = 0; }
    } vertexArray;

    // ========================================================================
    // Query and Sync State
    // ========================================================================

    struct QueryState {
        UnorderedMap<GLuint, GLuint> queryMap;
        UnorderedMap<GLuint, GLuint> queryMapReverse;
        void Reset() {
            queryMap.clear();
            queryMapReverse.clear();
        }
    } query;

    // ========================================================================
    // Transform Feedback State
    // ========================================================================

    struct TransformFeedbackState {
        UnorderedMap<GLuint, GLuint> tfMap;
        UnorderedMap<GLuint, GLuint> tfMapReverse;
        GLuint currentTF = 0;
        void Reset() {
            tfMap.clear();
            tfMapReverse.clear();
            currentTF = 0;
        }
    } transformFeedback;

    // ========================================================================
    // Renderbuffer State
    // ========================================================================

    struct RenderbufferState {
        UnorderedMap<GLuint, GLuint> rbMap;
        UnorderedMap<GLuint, GLuint> rbMapReverse;
        void Reset() {
            rbMap.clear();
            rbMapReverse.clear();
        }
    } renderbuffer;

    // ========================================================================
    // Utility: ID mapping helpers
    // ========================================================================

    // Get real GLES ID from virtual ID, returns 0 if not found
    GLuint GetRealBuffer(GLuint virtualId) {
        auto it = buffer.bufferMap.find(virtualId);
        return (it != buffer.bufferMap.end()) ? it->second : 0;
    }

    GLuint GetVirtualBuffer(GLuint realId) {
        auto it = buffer.bufferMapReverse.find(realId);
        return (it != buffer.bufferMapReverse.end()) ? it->second : 0;
    }

    GLuint GetRealTexture(GLuint virtualId) {
        auto it = texture.textureMap.find(virtualId);
        return (it != texture.textureMap.end()) ? it->second : 0;
    }

    GLuint GetVirtualTexture(GLuint realId) {
        auto it = texture.textureMapReverse.find(realId);
        return (it != texture.textureMapReverse.end()) ? it->second : 0;
    }

    GLuint GetRealFBO(GLuint virtualId) {
        auto it = framebuffer.fboMap.find(virtualId);
        return (it != framebuffer.fboMap.end()) ? it->second : 0;
    }

    GLuint GetVirtualFBO(GLuint realId) {
        auto it = framebuffer.fboMapReverse.find(realId);
        return (it != framebuffer.fboMapReverse.end()) ? it->second : 0;
    }

    GLuint GetRealVAO(GLuint virtualId) {
        auto it = buffer.vaoMap.find(virtualId);
        return (it != buffer.vaoMap.end()) ? it->second : 0;
    }

    GLuint GetVirtualVAO(GLuint realId) {
        auto it = buffer.vaoMapReverse.find(realId);
        return (it != buffer.vaoMapReverse.end()) ? it->second : 0;
    }

    GLuint GetRealShader(GLuint virtualId) {
        auto it = shader.shaderMap.find(virtualId);
        return (it != shader.shaderMap.end()) ? it->second : 0;
    }

    GLuint GetRealProgram(GLuint virtualId) {
        auto it = shader.programMap.find(virtualId);
        return (it != shader.programMap.end()) ? it->second : 0;
    }

    // ========================================================================
    // Utility: Format conversion helpers
    // ========================================================================

    static GLenum ConvertTextureTarget(GLenum target);
    static GLenum ConvertInternalFormat(GLenum format);
    static GLenum ConvertFormat(GLenum format);
    static GLenum ConvertType(GLenum type);
    static bool IsDepthStencilFormat(GLenum format);
    static bool IsCompressedFormat(GLenum format);
    static GLenum GetTextureBindingTarget(GLenum target);
    static GLenum TargetToBindingTarget(GLenum target);

    // ========================================================================
    // Thread safety
    // ========================================================================

    std::recursive_mutex mutex;

    void Lock() { mutex.lock(); }
    void Unlock() { mutex.unlock(); }

private:
    GLStateManager() = default;
    ~GLStateManager() = default;
    GLStateManager(const GLStateManager&) = delete;
    GLStateManager& operator=(const GLStateManager&) = delete;
};

// ============================================================================
// Convenience alias
// ============================================================================

#define GLState GLStateManager::Instance()

// ============================================================================
// Draw preparation macro
// ============================================================================

// This macro is called before every draw call to ensure sampler buffer
// (texture buffer) uniforms are updated with the correct texture IDs.
// The principle: texture buffers are emulated using regular textures,
// and the sampler uniform needs to be set to the texture unit where
// the emulated texture is bound.
#define PREPARE_FOR_DRAW()                                                              \
    do {                                                                                \
        if (GLState.emulateTextureBuffer) {                                              \
            GLState.isDrawCall = true;                                                   \
            /* Update sampler buffer textures for atomic counter emulation */            \
            for (auto& pair : GLState.buffer.texBuffers) {                               \
                GLuint virtualBuf = pair.first;                                          \
                auto& slot = pair.second;                                                \
                if (slot.texture && slot.buffer) {                                       \
                    GLint texUnit = GLState.currentTexUnit;                              \
                    glActiveTexture(GL_TEXTURE0 + texUnit);                              \
                    glBindTexture(GL_TEXTURE_2D, slot.texture);                          \
                    /* Set sampler uniform via the current program if known */           \
                    if (GLState.currentProgram) {                                        \
                        GLint loc = glGetUniformLocation(GLState.currentProgram,         \
                            ("mgTexBuf" + std::to_string(virtualBuf)).c_str());          \
                        if (loc >= 0) glUniform1i(loc, texUnit);                         \
                    }                                                                     \
                    glActiveTexture(GL_TEXTURE0 + GLState.currentTexUnit);                \
                }                                                                         \
            }                                                                             \
            GLState.isDrawCall = false;                                                   \
        }                                                                                 \
    } while (0)