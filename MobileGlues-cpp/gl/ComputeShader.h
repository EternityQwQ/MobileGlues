// MobileGlues - gl/ComputeShader.h
// ARB_compute_shader extension implemented on OpenGL ES 3.2
//
// Architecture principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// This header declares the internal functions used by the ARB_compute_shader
// extension implementation. The public API entry points are defined elsewhere:
//   - drawing.cpp: glDispatchCompute, glBindImageTexture, glMemoryBarrier
//   - gl_native.cpp: glDispatchComputeIndirect, glMemoryBarrierByRegion
//   - gl_stub.cpp: glDispatchComputeGroupSizeARB
//   - ExtWrappers/MultiBindWrapper.cpp: glBindImageTextures
//
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#ifndef MOBILEGLUES_COMPUTE_SHADER_H
#define MOBILEGLUES_COMPUTE_SHADER_H

#include "../includes.h"
#include <GL/gl.h>
#include "glcorearb.h"
#include <GLES3/gl32.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// State management
// ============================================================================

// Called when a compute program is activated (via glUseProgram).
// Queries the shader's compiled work group size for use by
// mgDispatchComputeGroupSizeARB.
void ComputeShader_OnUseProgram(GLuint program);

// ============================================================================
// ARB_compute_variable_group_size emulation
// ============================================================================

// Dispatch compute work groups with a variable local size.
// ES 3.2 does not support variable group size natively, so this function
// adjusts the number of work groups to match the requested total thread count.
//
// Parameters:
//   num_groups_x/y/z - number of work groups in each dimension
//   group_size_x/y/z - desired local work group size per dimension
//
// Implementation:
//   1. total_threads = num_groups * group_size
//   2. adjusted_groups = ceil(total_threads / shader_local_size)
//   3. glDispatchCompute(adjusted_groups)
void mgDispatchComputeGroupSizeARB(GLuint num_groups_x, GLuint num_groups_y,
                                   GLuint num_groups_z,
                                   GLuint group_size_x, GLuint group_size_y,
                                   GLuint group_size_z);

#ifdef __cplusplus
}
#endif

#endif // MOBILEGLUES_COMPUTE_SHADER_H