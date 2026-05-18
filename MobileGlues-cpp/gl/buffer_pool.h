// MobileGlues - gl/buffer_pool.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef MOBILEGLUES_BUFFER_POOL_H
#define MOBILEGLUES_BUFFER_POOL_H

#include <GL/gl.h>

#ifdef __cplusplus

void BufferPool_Init();

GLuint BufferPool_Acquire(GLenum target, GLsizeiptr minSize, GLenum usage);

void BufferPool_Release(GLuint buffer, GLsizeiptr usedSize);

void BufferPool_Cleanup(size_t maxTotalSize);

void BufferPool_Destroy();

#endif

#endif