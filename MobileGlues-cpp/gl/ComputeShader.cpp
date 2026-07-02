// MobileGlues - gl/ComputeShader.cpp
// ARB_compute_shader extension implemented on OpenGL ES 3.2
//
// Architecture principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// This module provides the internal implementation for the ARB_compute_shader
// extension. The public API functions are defined in gl_stub.cpp (for non-native
// functions) and drawing.cpp (for native functions with extra state tracking).
//
// Functions provided:
//   mgDispatchComputeGroupSizeARB → ARB_compute_variable_group_size emulation
//   ComputeShader_OnUseProgram    → called when a compute program is activated
//
// glDispatchComputeGroupSizeARB rationale:
//   ES 3.2 does not support variable work group size at dispatch time.
//   The work group size is baked into the compiled shader (local_size_x/y/z).
//   For the ARB_compute_variable_group_size compatibility shim, we:
//   1. Query the shader's compiled local work group size
//   2. Adjust the number of groups to compensate for the difference
//   3. Dispatch with the adjusted group count
//   This preserves the same total number of invocations.
//
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "ComputeShader.h"
#include "../gles/gles.h"
#include "state.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Internal state tracking for compute shader dispatch
// ---------------------------------------------------------------------------

// Track the currently bound compute program with its work group size
// for use by mgDispatchComputeGroupSizeARB
static GLuint compute_shader_program = 0;
static GLint  compute_shader_local_size[3] = { 1, 1, 1 };
static bool   compute_shader_local_size_valid = false;

// ---------------------------------------------------------------------------
// Helper: query the current program's compute shader work group size
// ---------------------------------------------------------------------------
static void query_compute_work_group_size(GLuint program) {
    compute_shader_local_size_valid = false;
    if (program == 0) return;

    // Use glGetProgramiv with GL_COMPUTE_WORK_GROUP_SIZE (ES 3.1+)
    // GL_COMPUTE_WORK_GROUP_SIZE = 0x8267, returns 3 ints: x, y, z
    GLint results[3] = { 0, 0, 0 };
    GLES.glGetProgramiv(program, 0x8267, results);

    // Check if the query returned valid data
    if (results[0] > 0) {
        compute_shader_local_size[0] = results[0];
        compute_shader_local_size[1] = (results[1] > 0) ? results[1] : 1;
        compute_shader_local_size[2] = (results[2] > 0) ? results[2] : 1;
        compute_shader_local_size_valid = true;
    }
}

// ---------------------------------------------------------------------------
// Exposed state update - called when a compute program is used
// ---------------------------------------------------------------------------
extern "C" void ComputeShader_OnUseProgram(GLuint program) {
    compute_shader_program = program;
    compute_shader_local_size_valid = false;

    if (program != 0) {
        // Check if this program has a compute shader attached
        GLint attached = 0;
        GLES.glGetProgramiv(program, GL_ATTACHED_SHADERS, &attached);
        if (attached > 0) {
            query_compute_work_group_size(program);
        }
    }
}

// ---------------------------------------------------------------------------
// mgDispatchComputeGroupSizeARB
// ARB_compute_variable_group_size emulation
//
// ES 3.2 does NOT support variable group size. The work group size is baked
// into the shader at compile time. For compatibility, we compute the adjusted
// number of groups to achieve the same total number of threads.
//
// Formula:
//   total_invocations = num_groups * group_size
//   adjusted_groups = ceil(total_invocations / local_size)
//
// This preserves the same total thread count, but the distribution of
// work across threads may differ from the original intent.
// ---------------------------------------------------------------------------
extern "C" void mgDispatchComputeGroupSizeARB(GLuint num_groups_x, GLuint num_groups_y,
                                              GLuint num_groups_z,
                                              GLuint group_size_x, GLuint group_size_y,
                                              GLuint group_size_z) {
    // If we have a valid program with a compute shader, adjust groups
    if (compute_shader_local_size_valid) {
        // Compute the total number of threads requested
        GLuint total_x = num_groups_x * group_size_x;
        GLuint total_y = num_groups_y * group_size_y;
        GLuint total_z = num_groups_z * group_size_z;

        // Adjust groups to match the shader's fixed local size
        GLuint adj_x = (total_x + compute_shader_local_size[0] - 1) / compute_shader_local_size[0];
        GLuint adj_y = (total_y + compute_shader_local_size[1] - 1) / compute_shader_local_size[1];
        GLuint adj_z = (total_z + compute_shader_local_size[2] - 1) / compute_shader_local_size[2];

        GLES.glDispatchCompute(adj_x, adj_y, adj_z);
    } else {
        // No compute program or unknown work group size - use the shader's
        // default local size. This passes through directly since we can't
        // adjust without knowing the local size.
        GLES.glDispatchCompute(num_groups_x, num_groups_y, num_groups_z);
    }
}