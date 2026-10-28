#include "UiGL.h"

namespace UiGL {

    GLuint(UGL_APIENTRY* CreateShader)(GLenum) = nullptr;
    void(UGL_APIENTRY* ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void(UGL_APIENTRY* CompileShader)(GLuint) = nullptr;
    void(UGL_APIENTRY* GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void(UGL_APIENTRY* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void(UGL_APIENTRY* DeleteShader)(GLuint) = nullptr;
    GLuint(UGL_APIENTRY* CreateProgram)() = nullptr;
    void(UGL_APIENTRY* AttachShader)(GLuint, GLuint) = nullptr;
    void(UGL_APIENTRY* DetachShader)(GLuint, GLuint) = nullptr;
    void(UGL_APIENTRY* LinkProgram)(GLuint) = nullptr;
    void(UGL_APIENTRY* GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void(UGL_APIENTRY* GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void(UGL_APIENTRY* DeleteProgram)(GLuint) = nullptr;
    void(UGL_APIENTRY* UseProgram)(GLuint) = nullptr;
    GLint(UGL_APIENTRY* GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void(UGL_APIENTRY* Uniform1f)(GLint, GLfloat) = nullptr;
    void(UGL_APIENTRY* Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void(UGL_APIENTRY* Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void(UGL_APIENTRY* Uniform1i)(GLint, GLint) = nullptr;
    void(UGL_APIENTRY* UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;
    void(UGL_APIENTRY* GenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void(UGL_APIENTRY* BindVertexArray)(GLuint) = nullptr;
    void(UGL_APIENTRY* GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void(UGL_APIENTRY* BindFramebuffer)(GLenum, GLuint) = nullptr;
    void(UGL_APIENTRY* FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void(UGL_APIENTRY* DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void(UGL_APIENTRY* GenerateMipmap)(GLenum) = nullptr;
    void(UGL_APIENTRY* ActiveTexture)(GLenum) = nullptr;
    void(UGL_APIENTRY* BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = nullptr;
    void(UGL_APIENTRY* BlendEquation)(GLenum) = nullptr;
    void(UGL_APIENTRY* BlendColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void(UGL_APIENTRY* BindBuffer)(GLenum, GLuint) = nullptr;

    void(UGL_APIENTRY* GenTextures)(GLsizei, GLuint*) = nullptr;
    void(UGL_APIENTRY* DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void(UGL_APIENTRY* BindTexture)(GLenum, GLuint) = nullptr;
    void(UGL_APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                   const void*) = nullptr;
    void(UGL_APIENTRY* TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                                      const void*) = nullptr;
    void(UGL_APIENTRY* TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void(UGL_APIENTRY* CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = nullptr;
    void(UGL_APIENTRY* DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void(UGL_APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void(UGL_APIENTRY* Scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void(UGL_APIENTRY* GetIntegerv)(GLenum, GLint*) = nullptr;
    void(UGL_APIENTRY* Enable)(GLenum) = nullptr;
    void(UGL_APIENTRY* Disable)(GLenum) = nullptr;
    GLboolean(UGL_APIENTRY* IsEnabled)(GLenum) = nullptr;
    void(UGL_APIENTRY* BlendFunc)(GLenum, GLenum) = nullptr;
    void(UGL_APIENTRY* PixelStorei)(GLenum, GLint) = nullptr;
    void(UGL_APIENTRY* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void(UGL_APIENTRY* Clear)(GLbitfield) = nullptr;
    void(UGL_APIENTRY* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;

    static bool loadedFlag = false;

#if defined(_WIN32)
    static void* defaultLoader(const char* name) {
        void* proc = (void*) wglGetProcAddress(name);
        if (proc == nullptr || proc == (void*) 0x1 || proc == (void*) 0x2 || proc == (void*) 0x3 ||
            proc == (void*) -1) {
            static HMODULE module = LoadLibraryA("opengl32.dll");
            proc = module ? (void*) GetProcAddress(module, name) : nullptr;
        }
        return proc;
    }
#else
    static void* defaultLoader(const char*) {
        return nullptr;
    }
#endif

    template <typename T>
    static bool bind(T& target, LoaderFn loader, const char* name) {
        target = reinterpret_cast<T>(loader(name));
        return target != nullptr;
    }

    bool load(LoaderFn loader) {
        if (loadedFlag) {
            return true;
        }
        LoaderFn fn = loader ? loader : defaultLoader;
        bool ok = true;
        ok &= bind(CreateShader, fn, "glCreateShader");
        ok &= bind(ShaderSource, fn, "glShaderSource");
        ok &= bind(CompileShader, fn, "glCompileShader");
        ok &= bind(GetShaderiv, fn, "glGetShaderiv");
        ok &= bind(GetShaderInfoLog, fn, "glGetShaderInfoLog");
        ok &= bind(DeleteShader, fn, "glDeleteShader");
        ok &= bind(CreateProgram, fn, "glCreateProgram");
        ok &= bind(AttachShader, fn, "glAttachShader");
        ok &= bind(DetachShader, fn, "glDetachShader");
        ok &= bind(LinkProgram, fn, "glLinkProgram");
        ok &= bind(GetProgramiv, fn, "glGetProgramiv");
        ok &= bind(GetProgramInfoLog, fn, "glGetProgramInfoLog");
        ok &= bind(DeleteProgram, fn, "glDeleteProgram");
        ok &= bind(UseProgram, fn, "glUseProgram");
        ok &= bind(GetUniformLocation, fn, "glGetUniformLocation");
        ok &= bind(Uniform1f, fn, "glUniform1f");
        ok &= bind(Uniform2f, fn, "glUniform2f");
        ok &= bind(Uniform4f, fn, "glUniform4f");
        ok &= bind(Uniform1i, fn, "glUniform1i");
        ok &= bind(UniformMatrix4fv, fn, "glUniformMatrix4fv");
        ok &= bind(GenVertexArrays, fn, "glGenVertexArrays");
        ok &= bind(BindVertexArray, fn, "glBindVertexArray");
        ok &= bind(GenFramebuffers, fn, "glGenFramebuffers");
        ok &= bind(BindFramebuffer, fn, "glBindFramebuffer");
        ok &= bind(FramebufferTexture2D, fn, "glFramebufferTexture2D");
        ok &= bind(DeleteFramebuffers, fn, "glDeleteFramebuffers");
        ok &= bind(GenerateMipmap, fn, "glGenerateMipmap");
        ok &= bind(ActiveTexture, fn, "glActiveTexture");
        ok &= bind(BlendFuncSeparate, fn, "glBlendFuncSeparate");
        ok &= bind(BlendEquation, fn, "glBlendEquation");
        ok &= bind(BlendColor, fn, "glBlendColor");
        ok &= bind(BindBuffer, fn, "glBindBuffer");
        ok &= bind(GenTextures, fn, "glGenTextures");
        ok &= bind(DeleteTextures, fn, "glDeleteTextures");
        ok &= bind(BindTexture, fn, "glBindTexture");
        ok &= bind(TexImage2D, fn, "glTexImage2D");
        ok &= bind(TexSubImage2D, fn, "glTexSubImage2D");
        ok &= bind(TexParameteri, fn, "glTexParameteri");
        ok &= bind(CopyTexSubImage2D, fn, "glCopyTexSubImage2D");
        ok &= bind(DrawArrays, fn, "glDrawArrays");
        ok &= bind(Viewport, fn, "glViewport");
        ok &= bind(Scissor, fn, "glScissor");
        ok &= bind(GetIntegerv, fn, "glGetIntegerv");
        ok &= bind(Enable, fn, "glEnable");
        ok &= bind(Disable, fn, "glDisable");
        ok &= bind(IsEnabled, fn, "glIsEnabled");
        ok &= bind(BlendFunc, fn, "glBlendFunc");
        ok &= bind(PixelStorei, fn, "glPixelStorei");
        ok &= bind(ClearColor, fn, "glClearColor");
        ok &= bind(Clear, fn, "glClear");
        ok &= bind(ReadPixels, fn, "glReadPixels");
        loadedFlag = ok;
        return ok;
    }

    bool loaded() {
        return loadedFlag;
    }
}
