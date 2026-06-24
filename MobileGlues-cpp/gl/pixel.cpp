// MobileGlues - gl/pixel.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

// ============================================================================
// Pixel Format Conversion Architecture
//
// Principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
//
// This file implements CPU-side pixel format conversion utilities needed
// because OpenGL supports more pixel formats than OpenGL ES 3.2.
//
// When a desktop OpenGL pixel format is not natively supported by ES 3.2,
// the pixel data is converted on the CPU before being passed to the native
// GLES implementation. This is CPU simulation, not GPU simulation.
//
// Supported conversions:
//   - Format size queries (gl_sizeof, pixel_sizeof)
//   - Packed type detection (is_type_packed)
//   - Color channel layout mapping (get_color_map)
//   - Full pixel data conversion (pixel_convert):
//       BGRA ↔ RGBA, LUMINANCE → RGBA/RGB, BGR → RGB/RGBA,
//       RGBA/BGRA → RGB565/RGBA5551/RGBA4444, unpacked → packed, etc.
// ============================================================================

#include "pixel.h"
#include "log.h"
#include "mg.h"

#define DEBUG 0

// ============================================================================
// Section: Type Size Query
// ============================================================================

// Sorted: GL type → size in bytes
struct TypeSizeEntry {
    GLenum key;
    GLsizei value;
};

static const TypeSizeEntry kTypeSizeTable[] = {
    {GL_BYTE,                          1},
    {GL_UNSIGNED_BYTE,                 1},
    {GL_SHORT,                         2},
    {GL_UNSIGNED_SHORT,                2},
    {GL_INT,                           4},
    {GL_UNSIGNED_INT,                  4},
    {GL_FLOAT,                         4},
    {GL_2_BYTES,                       2},
    {GL_3_BYTES,                       3},
    {GL_4_BYTES,                       4},
    {GL_DOUBLE,                        8},
    {GL_HALF_FLOAT,                    2},
    {GL_COLOR_INDEX,                   1},
    {GL_DEPTH_COMPONENT,               1},
    {GL_ALPHA,                         1},
    {GL_LUMINANCE,                     1},
    {GL_LUMINANCE_ALPHA,               2},
    {GL_UNSIGNED_BYTE_3_3_2,           1},
    {GL_UNSIGNED_SHORT_4_4_4_4,        2},
    {GL_UNSIGNED_SHORT_5_5_5_1,        2},
    {GL_UNSIGNED_INT_8_8_8_8,          4},
    {GL_UNSIGNED_BYTE_2_3_3_REV,       1},
    {GL_UNSIGNED_SHORT_5_6_5,          2},
    {GL_UNSIGNED_SHORT_5_6_5_REV,      2},
    {GL_UNSIGNED_SHORT_4_4_4_4_REV,    2},
    {GL_UNSIGNED_SHORT_1_5_5_5_REV,    2},
    {GL_UNSIGNED_INT_8_8_8_8_REV,      4},
    {GL_UNSIGNED_INT_10_10_10_2,       4},
    {GL_UNSIGNED_INT_2_10_10_10_REV,   4},
    {GL_UNSIGNED_INT_24_8,             4},
};
static constexpr size_t kTypeSizeTableCount = sizeof(kTypeSizeTable) / sizeof(kTypeSizeTable[0]);

GLsizei gl_sizeof(GLenum type) {
    size_t lo = 0, hi = kTypeSizeTableCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kTypeSizeTable[mid].key < type) lo = mid + 1;
        else hi = mid;
    }
    if (lo < kTypeSizeTableCount && kTypeSizeTable[lo].key == type) [[likely]]
        return kTypeSizeTable[lo].value;
    LOG_D("Unsupported pixel data type: %s\n", glEnumToString(type))
    return 0;
}

// ============================================================================
// Section: Packed Type Detection
// ============================================================================

// Sorted: packed type entries for binary search
static const GLenum kPackedTypes[] = {
    GL_2_BYTES,
    GL_3_BYTES,
    GL_4_BYTES,
    GL_UNSIGNED_BYTE_3_3_2,
    GL_UNSIGNED_SHORT_4_4_4_4,
    GL_UNSIGNED_SHORT_5_5_5_1,
    GL_UNSIGNED_INT_8_8_8_8,
    GL_UNSIGNED_BYTE_2_3_3_REV,
    GL_UNSIGNED_SHORT_5_6_5,
    GL_UNSIGNED_SHORT_5_6_5_REV,
    GL_UNSIGNED_SHORT_4_4_4_4_REV,
    GL_UNSIGNED_SHORT_1_5_5_5_REV,
    GL_UNSIGNED_INT_8_8_8_8_REV,
    GL_UNSIGNED_INT_10_10_10_2,
    GL_UNSIGNED_INT_2_10_10_10_REV,
    GL_DEPTH_STENCIL,
};
static constexpr size_t kPackedTypeCount = sizeof(kPackedTypes) / sizeof(kPackedTypes[0]);

GLboolean is_type_packed(GLenum type) {
    size_t lo = 0, hi = kPackedTypeCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kPackedTypes[mid] < type) lo = mid + 1;
        else hi = mid;
    }
    return (lo < kPackedTypeCount && kPackedTypes[lo] == type);
}

// ============================================================================
// Section: Pixel Size Calculation
// ============================================================================

// Sorted: format → component count
static const TypeSizeEntry kFormatWidthTable[] = {
    {GL_COLOR_INDEX,     1},
    {GL_DEPTH_COMPONENT, 1},
    {GL_RED,             1},
    {GL_ALPHA,           1},
    {GL_RGB,             3},
    {GL_RGBA,            4},
    {GL_LUMINANCE,       1},
    {GL_LUMINANCE_ALPHA, 2},
    {GL_R,               1},
    {GL_RGB8,            3},
    {GL_RGBA8,           4},
    {GL_BGR,             3},
    {GL_BGRA,            4},
    {GL_RG,              2},
    {GL_R32F,            4},
    {GL_DEPTH_STENCIL,   1},
    {GL_R11F_G11F_B10F,  4},
};
static constexpr size_t kFormatWidthTableCount = sizeof(kFormatWidthTable) / sizeof(kFormatWidthTable[0]);

GLsizei pixel_sizeof(GLenum format, GLenum type) {
    GLsizei width = 0;
    size_t lo = 0, hi = kFormatWidthTableCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kFormatWidthTable[mid].key < format) lo = mid + 1;
        else hi = mid;
    }
    if (lo < kFormatWidthTableCount && kFormatWidthTable[lo].key == format) [[likely]] {
        width = kFormatWidthTable[lo].value;
    } else {
        LOG_D("unsupported pixel format %s\n", glEnumToString(format))
        return 0;
    }

    if (is_type_packed(type)) width = 1;

    return width * gl_sizeof(type);
}

// ============================================================================
// Section: Color Channel Layout Mapping
// ============================================================================

static const colorlayout_t* get_color_map(GLenum format) {
#define map(fmt, ...)                                                                                                  \
    case fmt: {                                                                                                        \
        static colorlayout_t layout = {fmt, __VA_ARGS__};                                                              \
        return &layout;                                                                                                \
    }
    switch (format) {
        map(GL_RED, 0, -1, -1, -1, 0) map(GL_R, 0, -1, -1, -1, 0) map(GL_RG, 0, 1, -1, -1, 0)
            map(GL_RGBA, 0, 1, 2, 3, 3) map(GL_RGB, 0, 1, 2, -1, 2) map(GL_BGRA, 2, 1, 0, 3, 3)
                map(GL_BGR, 2, 1, 0, -1, 2) map(GL_LUMINANCE_ALPHA, 0, 0, 0, 1, 1) map(GL_LUMINANCE, 0, 0, 0, -1, 0)
                    map(GL_ALPHA, -1, -1, -1, 0, 0) map(GL_DEPTH_COMPONENT, 0, -1, -1, -1, 0)
                        map(GL_COLOR_INDEX, 0, 1, 2, 3, 3) default
            : LOG_D("get_color_map: unknown pixel format %s\n", glEnumToString(format)) break;
    }
    static colorlayout_t null = {0};
    return &null;
#undef map
}

// ============================================================================
// Section: Pixel Format Conversion (CPU Simulation)
//
// Converts pixel data between different format/type combinations on the CPU.
// This is needed when the source format is not natively supported by ES 3.2
// but the destination format is (or vice versa).
//
// Conversion rules:
//   1. Same format & type → memcpy (fast path)
//   2. Specialized fast paths for common conversions (BGRA↔RGBA, etc.)
//   3. Fallback: no general conversion, returns true
// ============================================================================

bool pixel_convert(const GLvoid* src, GLvoid** dst, GLuint width, GLuint height, GLenum src_format, GLenum src_type,
                   GLenum dst_format, GLenum dst_type, GLuint stride, GLuint align) {
    const colorlayout_t *src_color, *dst_color;
    GLuint pixels = width * height;
    if (src_type == GL_INT8_REV) src_type = GL_UNSIGNED_BYTE;
    if (dst_type == GL_INT8_REV) dst_type = GL_UNSIGNED_BYTE;
    GLuint dst_size = height * widthalign(width * pixel_sizeof(dst_format, dst_type), align);
    GLuint dst_width2 = widthalign((stride ? stride : width) * pixel_sizeof(dst_format, dst_type), align);
    GLuint dst_width = dst_width2 - (width * pixel_sizeof(dst_format, dst_type));
    GLuint src_width = widthalign(width * pixel_sizeof(src_format, src_type), align);
    GLuint src_widthadj = src_width - (width * pixel_sizeof(src_format, src_type));

    // ---------------------------------------------------------------------------
    // Fast path: same format and type → memcpy (no conversion needed)
    // ---------------------------------------------------------------------------
    if ((src_type == dst_type) && (dst_format == src_format)) {
        if (*dst == src) return true;
        if (!dst_size || !pixel_sizeof(src_format, src_type)) {
            LOG_D("pixel_convert: pixel conversion, unknown format size, anticipated "
                  "abort\n")
            return false;
        }
        if (*dst == nullptr) // alloc dst only if dst==NULL
            *dst = malloc(dst_size);
        if (stride) // for in-place conversion
            for (int yy = 0; yy < height; yy++)
                memcpy((char*)(*dst) + yy * dst_width2, (char*)src + yy * src_width, src_width);
        else
            memcpy(*dst, src, dst_size);
        return true;
    }

    src_color = get_color_map(src_format);
    dst_color = get_color_map(dst_format);
    if (!dst_size || !pixel_sizeof(src_format, src_type) || !src_color->type || !dst_color->type) {
        LOG_D("pixel_convert: pixel conversion, anticipated abort\n")
        return false;
    }
    GLsizei src_stride = pixel_sizeof(src_format, src_type);
    GLsizei dst_stride = pixel_sizeof(dst_format, dst_type);
    if (*dst == src || *dst == nullptr) *dst = malloc(dst_size);
    uintptr_t src_pos = widthalign((uintptr_t)src, align);
    uintptr_t dst_pos = widthalign((uintptr_t)*dst, align);

    // ---------------------------------------------------------------------------
    // Fast path: BGRA ↔ RGBA with UNSIGNED_BYTE
    // ---------------------------------------------------------------------------
    if ((((src_format == GL_BGRA) && (dst_format == GL_RGBA)) ||
         ((src_format == GL_RGBA) && (dst_format == GL_BGRA))) &&
        (dst_type == GL_UNSIGNED_BYTE) && ((src_type == GL_UNSIGNED_BYTE))) {
        GLuint tmp;
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                tmp = *(const GLuint*)src_pos;
#ifdef __BIG_ENDIAN__
                *(GLuint*)dst_pos = (tmp & 0x00ff00ff) | ((tmp & 0x0000ff00) << 16) | ((tmp & 0xff000000) >> 16);
#else
                *(GLuint*)dst_pos = (tmp & 0xff00ff00) | ((tmp & 0x00ff0000) >> 16) | ((tmp & 0x000000ff) << 16);
#endif
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Fast path: RGBA/BGRA with GL_INT_8_8_8_8 ↔ GL_INT_8_8_8_8_REV
    // ---------------------------------------------------------------------------
    if ((src_format == dst_format) && (src_format == GL_RGBA || src_format == GL_BGRA) &&
        ((src_type == GL_INT8 && dst_type == GL_INT8_REV) || (src_type == GL_INT8_REV && dst_type == GL_INT8))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                ((char*)dst_pos)[0] = ((char*)src_pos)[3];
                ((char*)dst_pos)[1] = ((char*)src_pos)[2];
                ((char*)dst_pos)[2] = ((char*)src_pos)[1];
                ((char*)dst_pos)[3] = ((char*)src_pos)[0];
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

#ifdef __BIG_ENDIAN__
    // ---------------------------------------------------------------------------
    // Fast path: RGBA/BGRA with UNSIGNED_INT_8_8_8_8_REV ↔ UNSIGNED_BYTE (big endian)
    // ---------------------------------------------------------------------------
    if ((src_format == dst_format) && (src_format == GL_RGBA || src_format == GL_BGRA) &&
        ((src_type == GL_UNSIGNED_INT_8_8_8_8_REV && dst_type == GL_UNSIGNED_BYTE) ||
         (src_type == GL_UNSIGNED_BYTE && dst_type == GL_UNSIGNED_INT_8_8_8_8_REV))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                ((char*)dst_pos)[0] = ((char*)src_pos)[3];
                ((char*)dst_pos)[1] = ((char*)src_pos)[2];
                ((char*)dst_pos)[2] = ((char*)src_pos)[1];
                ((char*)dst_pos)[3] = ((char*)src_pos)[0];
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }
#endif

    // ---------------------------------------------------------------------------
    // BGRA1555 → RGBA5551
    // ---------------------------------------------------------------------------
    if ((src_format == GL_BGRA) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_SHORT_5_5_5_1) &&
        (src_type == GL_UNSIGNED_SHORT_1_5_5_5_REV)) {
        GLushort tmp;
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                tmp = *(GLushort*)src_pos;
                *(GLushort*)dst_pos = ((tmp & 0x8000) >> 15) | ((tmp & 0x7fff) << 1);
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // LUMINANCE → RGBA (expand grayscale to RGBA with alpha=255)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_LUMINANCE) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_BYTE) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                auto* byte_dst = (unsigned char*)dst_pos;
#ifdef __BIG_ENDIAN__
                byte_dst[1] = byte_dst[2] = byte_dst[3] = *(GLubyte*)src_pos;
                byte_dst[0] = 255;
#else
                byte_dst[0] = byte_dst[1] = byte_dst[2] = *(GLubyte*)src_pos;
                byte_dst[3] = 255;
#endif
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // LUMINANCE → RGB (expand grayscale to RGB)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_LUMINANCE) && (dst_format == GL_RGB) && (dst_type == GL_UNSIGNED_BYTE) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                auto* byte_dst = (unsigned char*)dst_pos;
                byte_dst[0] = byte_dst[1] = byte_dst[2] = *(GLubyte*)src_pos;
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // RGBA → LUMINANCE_ALPHA (RGB→L weighted conversion)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_RGBA) && (dst_format == GL_LUMINANCE_ALPHA) && (dst_type == GL_UNSIGNED_BYTE) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                auto* byte_src = (unsigned char*)src_pos;
#ifdef __BIG_ENDIAN__
                *(GLushort*)dst_pos =
                    ((((int)byte_src[3]) * 77 + ((int)byte_src[2]) * 151 + ((int)byte_src[1]) * 28) & 0xff00) >> 8 |
                    (byte_src[0] << 8);
#else
                *(GLushort*)dst_pos =
                    ((((int)byte_src[0]) * 77 + ((int)byte_src[1]) * 151 + ((int)byte_src[2]) * 28) & 0xff00) >> 8 |
                    (byte_src[3] << 8);
#endif
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGRA → LUMINANCE_ALPHA (BGR→L weighted conversion)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_BGRA) && (dst_format == GL_LUMINANCE_ALPHA) && (dst_type == GL_UNSIGNED_BYTE) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                auto* byte_src = (unsigned char*)src_pos;
#ifdef __BIG_ENDIAN__
                *(GLushort*)dst_pos =
                    ((((int)byte_src[1]) * 77 + ((int)byte_src[2]) * 151 + ((int)byte_src[3]) * 28) & 0xff00) >> 8 |
                    (byte_src[0] << 8);
#else
                *(GLushort*)dst_pos =
                    ((((int)byte_src[2]) * 77 + ((int)byte_src[1]) * 151 + ((int)byte_src[0]) * 28) & 0xff00) >> 8 |
                    (byte_src[3] << 8);
#endif
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // RGB(A) → LUMINANCE (weighted RGB→grayscale conversion)
    // ---------------------------------------------------------------------------
    if (((src_format == GL_RGBA) || (src_format == GL_RGB)) && (dst_format == GL_LUMINANCE) &&
        (dst_type == GL_UNSIGNED_BYTE) && ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                auto* byte_src = (unsigned char*)src_pos;
#ifdef __BIG_ENDIAN__
                *(unsigned char*)dst_pos =
                    (((int)byte_src[3]) * 77 + ((int)byte_src[2]) * 151 + ((int)byte_src[1]) * 28) >> 8;
#else
                *(unsigned char*)dst_pos =
                    (((int)byte_src[0]) * 77 + ((int)byte_src[1]) * 151 + ((int)byte_src[2]) * 28) >> 8;
#endif
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGR(A) → LUMINANCE (weighted BGR→grayscale conversion)
    // ---------------------------------------------------------------------------
    if (((src_format == GL_BGRA) || (src_format == GL_BGR)) && (dst_format == GL_LUMINANCE) &&
        (dst_type == GL_UNSIGNED_BYTE) && ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                auto* byte_src = (unsigned char*)src_pos;
#ifdef __BIG_ENDIAN__
                *(unsigned char*)dst_pos =
                    (((int)byte_src[1]) * 77 + ((int)byte_src[2]) * 151 + ((int)byte_src[3]) * 28) >> 8;
#else
                *(unsigned char*)dst_pos =
                    (((int)byte_src[2]) * 77 + ((int)byte_src[1]) * 151 + ((int)byte_src[0]) * 28) >> 8;
#endif
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGR(A) → RGB (channel swap)
    // ---------------------------------------------------------------------------
    if (((src_format == GL_BGR) || (src_format == GL_BGRA)) && (dst_format == GL_RGB) &&
        (dst_type == GL_UNSIGNED_BYTE) && ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                ((char*)dst_pos)[0] = ((char*)src_pos)[2];
                ((char*)dst_pos)[1] = ((char*)src_pos)[1];
                ((char*)dst_pos)[2] = ((char*)src_pos)[0];
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGR → RGBA (channel swap + alpha=255)
    // ---------------------------------------------------------------------------
    if (((src_format == GL_BGR)) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_BYTE) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                ((unsigned char*)dst_pos)[0] = ((unsigned char*)src_pos)[2];
                ((unsigned char*)dst_pos)[1] = ((unsigned char*)src_pos)[1];
                ((unsigned char*)dst_pos)[2] = ((unsigned char*)src_pos)[0];
                ((unsigned char*)dst_pos)[3] = 255;
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // RGBA → RGB (drop alpha channel)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_RGBA) && (dst_format == GL_RGB) && (dst_type == GL_UNSIGNED_BYTE) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                ((char*)dst_pos)[0] = ((char*)src_pos)[0];
                ((char*)dst_pos)[1] = ((char*)src_pos)[1];
                ((char*)dst_pos)[2] = ((char*)src_pos)[2];
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // RGB(A) → RGB565 (8-bit channels → packed 16-bit)
    // ---------------------------------------------------------------------------
    if (((src_format == GL_RGB) || (src_format == GL_RGBA)) && (dst_format == GL_RGB) &&
        (dst_type == GL_UNSIGNED_SHORT_5_6_5) && ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                *(GLushort*)dst_pos = ((GLushort)(((char*)src_pos)[2] & 0xf8) >> (3)) |
                                      ((GLushort)(((char*)src_pos)[1] & 0xfc) << (5 - 2)) |
                                      ((GLushort)(((char*)src_pos)[0] & 0xf8) << (11 - 3));
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGR(A) → RGB565 (8-bit BGR channels → packed 16-bit RGB565)
    // ---------------------------------------------------------------------------
    if (((src_format == GL_BGR) || (src_format == GL_BGRA)) && (dst_format == GL_RGB) &&
        (dst_type == GL_UNSIGNED_SHORT_5_6_5) && ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                *(GLushort*)dst_pos = ((GLushort)(((char*)src_pos)[0] & 0xf8) >> (3)) |
                                      ((GLushort)(((char*)src_pos)[1] & 0xfc) << (5 - 2)) |
                                      ((GLushort)(((char*)src_pos)[2] & 0xf8) << (11 - 3));
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // RGBA → RGBA5551 (8-bit channels → packed 16-bit RGBA5551)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_RGBA) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_SHORT_5_5_5_1) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                *(GLushort*)dst_pos = ((GLushort)(((char*)src_pos)[2] & 0xf8) >> (3 - 1)) |
                                      ((GLushort)(((char*)src_pos)[1] & 0xf8) << (5 - 2)) |
                                      ((GLushort)(((char*)src_pos)[0] & 0xf8) << (10 - 2)) |
                                      ((GLushort)(((char*)src_pos)[3]) ? 1 : 0);
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGRA → RGBA5551 (8-bit BGRA channels → packed 16-bit RGBA5551)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_BGRA) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_SHORT_5_5_5_1) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                *(GLushort*)dst_pos = ((GLushort)(((char*)src_pos)[0] & 0xf8) >> (3 - 1)) |
                                      ((GLushort)(((char*)src_pos)[1] & 0xf8) << (5 - 2)) |
                                      ((GLushort)(((char*)src_pos)[2] & 0xf8) << (10 - 2)) |
                                      ((GLushort)(((char*)src_pos)[3]) ? 1 : 0);
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // RGBA → RGBA4444 (8-bit channels → packed 16-bit RGBA4444)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_RGBA) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_SHORT_4_4_4_4) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                *(GLushort*)dst_pos =
                    ((GLushort)(((char*)src_pos)[3] & 0xf0)) >> (4) | ((GLushort)(((char*)src_pos)[2] & 0xf0)) |
                    ((GLushort)(((char*)src_pos)[1] & 0xf0)) << (4) | ((GLushort)(((char*)src_pos)[0] & 0xf0)) << (8);
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGRA → RGBA4444 (8-bit BGRA channels → packed 16-bit RGBA4444)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_BGRA) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_SHORT_4_4_4_4) &&
        ((src_type == GL_UNSIGNED_BYTE))) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                *(GLushort*)dst_pos =
                    ((GLushort)(((char*)src_pos)[3] & 0xf0) >> (4)) | ((GLushort)(((char*)src_pos)[0] & 0xf0)) |
                    ((GLushort)(((char*)src_pos)[1] & 0xf0) << (4)) | ((GLushort)(((char*)src_pos)[2] & 0xf0) << (8));
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // BGRA4444 → RGBA (unpacked 16-bit BGRA4444 → 8-bit RGBA)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_BGRA) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_BYTE) &&
        (src_type == GL_UNSIGNED_SHORT_4_4_4_4_REV)) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                const GLushort pix = *(GLushort*)src_pos;
                ((char*)dst_pos)[3] = ((pix >> 12) & 0x0f) << 4;
                ((char*)dst_pos)[2] = ((pix >> 8) & 0x0f) << 4;
                ((char*)dst_pos)[1] = ((pix >> 4) & 0x0f) << 4;
                ((char*)dst_pos)[0] = ((pix) & 0x0f) << 4;
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // RGBA5551 → RGBA (unpacked 16-bit RGBA5551 → 8-bit RGBA)
    // ---------------------------------------------------------------------------
    if ((src_format == GL_RGBA) && (dst_format == GL_RGBA) && (dst_type == GL_UNSIGNED_BYTE) &&
        (src_type == GL_UNSIGNED_SHORT_5_5_5_1)) {
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                const GLushort pix = *(GLushort*)src_pos;
                ((unsigned char*)dst_pos)[0] = ((pix >> 11) & 0x1f) << 3;
                ((unsigned char*)dst_pos)[1] = ((pix >> 6) & 0x1f) << 3;
                ((unsigned char*)dst_pos)[2] = ((pix >> 1) & 0x1f) << 3;
                ((unsigned char*)dst_pos)[3] = ((pix) & 0x01) ? 255 : 0;
                src_pos += src_stride;
                dst_pos += dst_stride;
            }
            dst_pos += dst_width;
            src_pos += src_widthadj;
        }
        return true;
    }

    return true;
}