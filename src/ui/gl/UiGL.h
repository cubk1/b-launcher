#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define UGL_APIENTRY APIENTRY
#else
#define UGL_APIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef std::ptrdiff_t GLsizeiptr;

#define UGL_FALSE 0
#define UGL_TRUE 1
#define UGL_TRIANGLES 0x0004
#define UGL_BLEND 0x0BE2
#define UGL_CULL_FACE 0x0B44
#define UGL_DEPTH_TEST 0x0B71
#define UGL_STENCIL_TEST 0x0B90
#define UGL_SCISSOR_TEST 0x0C11
#define UGL_SCISSOR_BOX 0x0C10
#define UGL_VIEWPORT 0x0BA2
#define UGL_SRC_ALPHA 0x0302
#define UGL_ONE_MINUS_SRC_ALPHA 0x0303
#define UGL_ONE_MINUS_SRC_COLOR 0x0301
#define UGL_CONSTANT_COLOR 0x8001
#define UGL_ONE 1
#define UGL_ZERO 0
#define UGL_FUNC_ADD 0x8006
#define UGL_TEXTURE_2D 0x0DE1
#define UGL_TEXTURE0 0x84C0
#define UGL_TEXTURE3 0x84C3
#define UGL_TEXTURE_MIN_FILTER 0x2801
#define UGL_TEXTURE_MAG_FILTER 0x2800
#define UGL_TEXTURE_WRAP_S 0x2802
#define UGL_TEXTURE_WRAP_T 0x2803
#define UGL_TEXTURE_BASE_LEVEL 0x813C
#define UGL_TEXTURE_MAX_LEVEL 0x813D
#define UGL_CLAMP_TO_EDGE 0x812F
#define UGL_LINEAR 0x2601
#define UGL_NEAREST 0x2600
#define UGL_LINEAR_MIPMAP_LINEAR 0x2703
#define UGL_RGBA 0x1908
#define UGL_RGBA8 0x8058
#define UGL_RGBA32F 0x8814
#define UGL_UNSIGNED_BYTE 0x1401
#define UGL_FLOAT 0x1406
#define UGL_FRAMEBUFFER 0x8D40
#define UGL_READ_FRAMEBUFFER 0x8CA8
#define UGL_DRAW_FRAMEBUFFER 0x8CA9
#define UGL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define UGL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define UGL_COLOR_ATTACHMENT0 0x8CE0
#define UGL_VERTEX_SHADER 0x8B31
#define UGL_FRAGMENT_SHADER 0x8B30
#define UGL_COMPILE_STATUS 0x8B81
#define UGL_LINK_STATUS 0x8B82
#define UGL_INFO_LOG_LENGTH 0x8B84
#define UGL_CURRENT_PROGRAM 0x8B8D
#define UGL_VERTEX_ARRAY_BINDING 0x85B5
#define UGL_TEXTURE_BINDING_2D 0x8069
#define UGL_ACTIVE_TEXTURE 0x84E0
#define UGL_PIXEL_UNPACK_BUFFER 0x88EC
#define UGL_UNPACK_ROW_LENGTH 0x0CF2
#define UGL_UNPACK_SKIP_PIXELS 0x0CF4
#define UGL_UNPACK_SKIP_ROWS 0x0CF3
#define UGL_UNPACK_ALIGNMENT 0x0CF5
#define UGL_PACK_ALIGNMENT 0x0D05
#define UGL_COLOR_BUFFER_BIT 0x00004000

namespace UiGL {

    using LoaderFn = void* (*) (const char* name);

    bool load(LoaderFn loader = nullptr);
    bool loaded();

    extern GLuint(UGL_APIENTRY* CreateShader)(GLenum type);
    extern void(UGL_APIENTRY* ShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string,
                                            const GLint* length);
    extern void(UGL_APIENTRY* CompileShader)(GLuint shader);
    extern void(UGL_APIENTRY* GetShaderiv)(GLuint shader, GLenum pname, GLint* params);
    extern void(UGL_APIENTRY* GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
    extern void(UGL_APIENTRY* DeleteShader)(GLuint shader);
    extern GLuint(UGL_APIENTRY* CreateProgram)();
    extern void(UGL_APIENTRY* AttachShader)(GLuint program, GLuint shader);
    extern void(UGL_APIENTRY* DetachShader)(GLuint program, GLuint shader);
    extern void(UGL_APIENTRY* LinkProgram)(GLuint program);
    extern void(UGL_APIENTRY* GetProgramiv)(GLuint program, GLenum pname, GLint* params);
    extern void(UGL_APIENTRY* GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
    extern void(UGL_APIENTRY* DeleteProgram)(GLuint program);
    extern void(UGL_APIENTRY* UseProgram)(GLuint program);
    extern GLint(UGL_APIENTRY* GetUniformLocation)(GLuint program, const GLchar* name);
    extern void(UGL_APIENTRY* Uniform1f)(GLint location, GLfloat v0);
    extern void(UGL_APIENTRY* Uniform2f)(GLint location, GLfloat v0, GLfloat v1);
    extern void(UGL_APIENTRY* Uniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
    extern void(UGL_APIENTRY* Uniform1i)(GLint location, GLint v0);
    extern void(UGL_APIENTRY* UniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose,
                                                const GLfloat* value);
    extern void(UGL_APIENTRY* GenVertexArrays)(GLsizei n, GLuint* arrays);
    extern void(UGL_APIENTRY* BindVertexArray)(GLuint array);
    extern void(UGL_APIENTRY* GenFramebuffers)(GLsizei n, GLuint* framebuffers);
    extern void(UGL_APIENTRY* BindFramebuffer)(GLenum target, GLuint framebuffer);
    extern void(UGL_APIENTRY* FramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                                    GLint level);
    extern void(UGL_APIENTRY* DeleteFramebuffers)(GLsizei n, const GLuint* framebuffers);
    extern void(UGL_APIENTRY* GenerateMipmap)(GLenum target);
    extern void(UGL_APIENTRY* ActiveTexture)(GLenum texture);
    extern void(UGL_APIENTRY* BlendFuncSeparate)(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha,
                                                 GLenum dfactorAlpha);
    extern void(UGL_APIENTRY* BlendEquation)(GLenum mode);
    extern void(UGL_APIENTRY* BlendColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    extern void(UGL_APIENTRY* BindBuffer)(GLenum target, GLuint buffer);

    extern void(UGL_APIENTRY* GenTextures)(GLsizei n, GLuint* textures);
    extern void(UGL_APIENTRY* DeleteTextures)(GLsizei n, const GLuint* textures);
    extern void(UGL_APIENTRY* BindTexture)(GLenum target, GLuint texture);
    extern void(UGL_APIENTRY* TexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width,
                                          GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
    extern void(UGL_APIENTRY* TexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                             GLsizei height, GLenum format, GLenum type, const void* pixels);
    extern void(UGL_APIENTRY* TexParameteri)(GLenum target, GLenum pname, GLint param);
    extern void(UGL_APIENTRY* CopyTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x,
                                                 GLint y, GLsizei width, GLsizei height);
    extern void(UGL_APIENTRY* DrawArrays)(GLenum mode, GLint first, GLsizei count);
    extern void(UGL_APIENTRY* Viewport)(GLint x, GLint y, GLsizei width, GLsizei height);
    extern void(UGL_APIENTRY* Scissor)(GLint x, GLint y, GLsizei width, GLsizei height);
    extern void(UGL_APIENTRY* GetIntegerv)(GLenum pname, GLint* data);
    extern void(UGL_APIENTRY* Enable)(GLenum cap);
    extern void(UGL_APIENTRY* Disable)(GLenum cap);
    extern GLboolean(UGL_APIENTRY* IsEnabled)(GLenum cap);
    extern void(UGL_APIENTRY* BlendFunc)(GLenum sfactor, GLenum dfactor);
    extern void(UGL_APIENTRY* PixelStorei)(GLenum pname, GLint param);
    extern void(UGL_APIENTRY* ClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    extern void(UGL_APIENTRY* Clear)(GLbitfield mask);
    extern void(UGL_APIENTRY* ReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                          void* pixels);
}
