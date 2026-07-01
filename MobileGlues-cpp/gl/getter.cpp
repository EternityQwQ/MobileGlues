// MobileGlues - gl/getter.cpp
// State query functions: glGetIntegerv with CPU-side state simulation
// for queries that are not natively supported in OpenGL ES 3.2.
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

#define DEBUG 0

// ============================================================================
// glGetError - always returns GL_NO_ERROR
// ============================================================================

GLenum glGetError() {
    return GL_NO_ERROR;
}

// ============================================================================
// glGetIntegerv - CPU-side state simulation for non-ES queries
// ============================================================================

void glGetIntegerv(GLenum pname, GLint *params) {
    LOG()
    auto &legacy = GLState.legacy;
    auto &fb = GLState.framebuffer;
    auto &bs = GLState.buffer;

    switch (pname) {
        case GL_VIEWPORT:
            params[0] = legacy.viewport[0];
            params[1] = legacy.viewport[1];
            params[2] = legacy.viewport[2];
            params[3] = legacy.viewport[3];
            break;

        case GL_SCISSOR_BOX:
            params[0] = legacy.scissor[0];
            params[1] = legacy.scissor[1];
            params[2] = legacy.scissor[2];
            params[3] = legacy.scissor[3];
            break;

        case GL_COLOR_CLEAR_VALUE:
            params[0] = (GLint)(legacy.clearColor[0] * 255);
            params[1] = (GLint)(legacy.clearColor[1] * 255);
            params[2] = (GLint)(legacy.clearColor[2] * 255);
            params[3] = (GLint)(legacy.clearColor[3] * 255);
            break;

        case GL_DEPTH_CLEAR_VALUE:
            *params = (GLint)legacy.clearDepth;
            break;

        case GL_STENCIL_CLEAR_VALUE:
            *params = legacy.clearStencil;
            break;

        case GL_COLOR_WRITEMASK:
            params[0] = legacy.colorMask[0];
            params[1] = legacy.colorMask[1];
            params[2] = legacy.colorMask[2];
            params[3] = legacy.colorMask[3];
            break;

        case GL_DEPTH_WRITEMASK:
            *params = legacy.depthMask;
            break;

        case GL_DEPTH_FUNC:
            *params = legacy.depthFunc;
            break;

        case GL_STENCIL_REF:
            *params = legacy.stencilRef;
            break;

        case GL_STENCIL_VALUE_MASK:
            *params = (GLint)legacy.stencilMask;
            break;

        case GL_STENCIL_WRITEMASK:
            *params = (GLint)legacy.stencilMask;
            break;

        case GL_BLEND_SRC:
        case GL_BLEND_SRC_RGB:
            *params = legacy.blendSrcRGB;
            break;

        case GL_BLEND_DST:
        case GL_BLEND_DST_RGB:
            *params = legacy.blendDstRGB;
            break;

        case GL_BLEND_SRC_ALPHA:
            *params = legacy.blendSrcAlpha;
            break;

        case GL_BLEND_DST_ALPHA:
            *params = legacy.blendDstAlpha;
            break;

        case GL_CULL_FACE_MODE:
            *params = legacy.cullFace;
            break;

        case GL_FRONT_FACE:
            *params = legacy.frontFace;
            break;

        case GL_POLYGON_MODE:
            params[0] = legacy.polygonMode[0];
            params[1] = legacy.polygonMode[1];
            break;

        case GL_LINE_WIDTH:
            *params = (GLint)legacy.lineWidth;
            break;

        case GL_ARRAY_BUFFER_BINDING:
            *params = (GLint)(bs.boundBuffer.count(GL_ARRAY_BUFFER) ? bs.boundBuffer[GL_ARRAY_BUFFER] : 0);
            break;

        case GL_ELEMENT_ARRAY_BUFFER_BINDING:
            *params = (GLint)(bs.boundBuffer.count(GL_ELEMENT_ARRAY_BUFFER) ? bs.boundBuffer[GL_ELEMENT_ARRAY_BUFFER] : 0);
            break;

        case GL_UNIFORM_BUFFER_BINDING:
            *params = (GLint)bs.uniformBufferBases[0];
            break;

        case GL_SHADER_STORAGE_BUFFER_BINDING:
            *params = (GLint)bs.shaderStorageBases[0];
            break;

        case GL_VERTEX_ARRAY_BINDING:
            *params = (GLint)GLState.vertexArray.currentVAO;
            break;

        case GL_TEXTURE_BINDING_1D:
        case GL_TEXTURE_BINDING_2D:
        case GL_TEXTURE_BINDING_3D:
        case GL_TEXTURE_BINDING_CUBE_MAP:
        case GL_TEXTURE_BINDING_1D_ARRAY:
        case GL_TEXTURE_BINDING_2D_ARRAY: {
            GLenum target = GL_TEXTURE_2D;
            switch (pname) {
                case GL_TEXTURE_BINDING_1D: target = GL_TEXTURE_1D; break;
                case GL_TEXTURE_BINDING_2D: target = GL_TEXTURE_2D; break;
                case GL_TEXTURE_BINDING_3D: target = GL_TEXTURE_3D; break;
                case GL_TEXTURE_BINDING_CUBE_MAP: target = GL_TEXTURE_CUBE_MAP; break;
                case GL_TEXTURE_BINDING_1D_ARRAY: target = GL_TEXTURE_1D_ARRAY; break;
                case GL_TEXTURE_BINDING_2D_ARRAY: target = GL_TEXTURE_2D_ARRAY; break;
            }
            int unit = GLState.texture.activeUnit;
            auto &bindings = GLState.texture.texUnits[unit].binding;
            *params = (GLint)(bindings.count(target) ? bindings[target] : 0);
            break;
        }

        case GL_ACTIVE_TEXTURE:
            *params = GL_TEXTURE0 + GLState.texture.activeUnit;
            break;

        case GL_DRAW_FRAMEBUFFER_BINDING:
            *params = (GLint)fb.drawFBO;
            break;

        case GL_READ_FRAMEBUFFER_BINDING:
            *params = (GLint)fb.readFBO;
            break;

        case GL_DRAW_BUFFER:
            *params = fb.drawBufferCount > 0 ? (GLint)fb.drawBuffers[0] : GL_BACK;
            break;

        case GL_READ_BUFFER:
            *params = (GLint)fb.readBuffer;
            break;

        case GL_CURRENT_PROGRAM:
            *params = (GLint)GLState.currentProgram;
            break;

        default:
            // Native passthrough for all other queries
            GLES.glGetIntegerv(pname, params);
            break;
    }
}