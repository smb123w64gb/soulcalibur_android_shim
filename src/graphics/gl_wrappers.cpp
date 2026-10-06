#include "graphics/gl_wrappers.h"
#include "graphics/viewport.h"
#include "core/hooks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#ifndef GLintptr
typedef ptrdiff_t GLintptr;
#endif
#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif
#ifndef GLchar
typedef char GLchar;
#endif

#define GL_ARRAY_BUFFER         0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ACTIVE_ATTRIBUTES    0x8B89

typedef void   (APIENTRY *PFN_glActiveTexture)(GLenum);
typedef void   (APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (APIENTRY *PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef void   (APIENTRY *PFN_glDisableVertexAttribArray)(GLuint);
typedef void   (APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (APIENTRY *PFN_glVertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *PFN_glUseProgram)(GLuint);
typedef void   (APIENTRY *PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (APIENTRY *PFN_glUniform4fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRY *PFN_glUniform1i)(GLint, GLint);
typedef void   (APIENTRY *PFN_glUniform1f)(GLint, GLfloat);
typedef void   (APIENTRY *PFN_glUniform2fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRY *PFN_glUniform3fv)(GLint, GLsizei, const GLfloat*);
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
typedef void   (APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void   (APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void   (APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLint  (APIENTRY *PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (APIENTRY *PFN_glBindAttribLocation)(GLuint, GLuint, const GLchar*);
typedef void   (APIENTRY *PFN_glDepthRangef)(GLclampf, GLclampf);
typedef void   (APIENTRY *PFN_glCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum);
typedef void   (APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRY *PFN_glCompileShader)(GLuint);
typedef void   (APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRY *PFN_glDeleteShader)(GLuint);
typedef void   (APIENTRY *PFN_glGetActiveAttrib)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*);
typedef GLint  (APIENTRY *PFN_glGetAttribLocation)(GLuint, const GLchar*);

// Extended GLES 2.0 PFNs
typedef void (APIENTRY *PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void (APIENTRY *PFN_glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (APIENTRY *PFN_glDeleteProgram)(GLuint);
typedef void (APIENTRY *PFN_glDetachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
typedef void (APIENTRY *PFN_glBlendEquation)(GLenum);
typedef void (APIENTRY *PFN_glBlendEquationSeparate)(GLenum, GLenum);
typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY *PFN_glGenerateMipmap)(GLenum);

// Function Pointers
static PFN_glActiveTexture           pfn_glActiveTexture = nullptr;
static PFN_glBindBuffer              pfn_glBindBuffer = nullptr;
static PFN_glBufferData              pfn_glBufferData = nullptr;
static PFN_glDeleteBuffers           pfn_glDeleteBuffers = nullptr;
static PFN_glGenBuffers              pfn_glGenBuffers = nullptr;
static PFN_glEnableVertexAttribArray pfn_glEnableVertexAttribArray = nullptr;
static PFN_glDisableVertexAttribArray pfn_glDisableVertexAttribArray = nullptr;
static PFN_glVertexAttribPointer     pfn_glVertexAttribPointer = nullptr;
static PFN_glVertexAttrib4f          pfn_glVertexAttrib4f = nullptr;
static PFN_glUseProgram              pfn_glUseProgram = nullptr;
static PFN_glUniformMatrix4fv        pfn_glUniformMatrix4fv = nullptr;
static PFN_glUniform4fv              pfn_glUniform4fv = nullptr;
static PFN_glUniform1i               pfn_glUniform1i = nullptr;
static PFN_glUniform1f               pfn_glUniform1f = nullptr;
static PFN_glUniform2fv              pfn_glUniform2fv = nullptr;
static PFN_glUniform3fv              pfn_glUniform3fv = nullptr;
static PFN_glCreateProgram           pfn_glCreateProgram = nullptr;
static PFN_glAttachShader            pfn_glAttachShader = nullptr;
static PFN_glLinkProgram             pfn_glLinkProgram = nullptr;
static PFN_glGetProgramiv            pfn_glGetProgramiv = nullptr;
static PFN_glGetProgramInfoLog       pfn_glGetProgramInfoLog = nullptr;
static PFN_glGetUniformLocation      pfn_glGetUniformLocation = nullptr;
static PFN_glBindAttribLocation      pfn_glBindAttribLocation = nullptr;
static PFN_glDepthRangef             pfn_glDepthRangef = nullptr;
static PFN_glCompressedTexImage2D    pfn_glCompressedTexImage2D = nullptr;
static PFN_glCreateShader            pfn_glCreateShader = nullptr;
static PFN_glShaderSource            pfn_glShaderSource = nullptr;
static PFN_glCompileShader           pfn_glCompileShader = nullptr;
static PFN_glGetShaderiv             pfn_glGetShaderiv = nullptr;
static PFN_glGetShaderInfoLog        pfn_glGetShaderInfoLog = nullptr;
static PFN_glDeleteShader            pfn_glDeleteShader = nullptr;
static PFN_glGetActiveAttrib         pfn_glGetActiveAttrib = nullptr;
static PFN_glGetAttribLocation       pfn_glGetAttribLocation = nullptr;
static PFN_glTexParameteri          pfn_glTexParameteri = nullptr;
static PFN_glBufferSubData           pfn_glBufferSubData = nullptr;
static PFN_glDeleteProgram           pfn_glDeleteProgram = nullptr;
static PFN_glDetachShader            pfn_glDetachShader = nullptr;
static PFN_glBlendFuncSeparate       pfn_glBlendFuncSeparate = nullptr;
static PFN_glBlendEquation           pfn_glBlendEquation = nullptr;
static PFN_glBlendEquationSeparate   pfn_glBlendEquationSeparate = nullptr;
static PFN_glGenFramebuffers         pfn_glGenFramebuffers = nullptr;
static PFN_glDeleteFramebuffers      pfn_glDeleteFramebuffers = nullptr;
static PFN_glBindFramebuffer         pfn_glBindFramebuffer = nullptr;
static PFN_glFramebufferTexture2D    pfn_glFramebufferTexture2D = nullptr;
static PFN_glGenerateMipmap          pfn_glGenerateMipmap = nullptr;

// State tracking
static GLuint   g_bound_array_buffer = 0;
static GLuint   g_bound_element_array_buffer = 0;
static GLuint   g_current_program = 0;
static uint32_t g_enabled_attrib_mask = 0;

static void __cdecl wrap_glActiveTexture(GLenum texture) {
    if (!pfn_glActiveTexture) pfn_glActiveTexture = (PFN_glActiveTexture)SDL_GL_GetProcAddress("glActiveTexture");
    if (pfn_glActiveTexture) pfn_glActiveTexture(texture);
}

static void __cdecl wrap_glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) g_bound_array_buffer = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) g_bound_element_array_buffer = buffer;
    if (!pfn_glBindBuffer) pfn_glBindBuffer = (PFN_glBindBuffer)SDL_GL_GetProcAddress("glBindBuffer");
    if (pfn_glBindBuffer) pfn_glBindBuffer(target, buffer);
}

static void __cdecl wrap_glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (!pfn_glBufferData) pfn_glBufferData = (PFN_glBufferData)SDL_GL_GetProcAddress("glBufferData");
    if (pfn_glBufferData) pfn_glBufferData(target, size, data, usage);
}

static void __cdecl wrap_glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if (buffers && n > 0) {
        for (GLsizei i = 0; i < n; i++) {
            if (buffers[i] == g_bound_array_buffer)         g_bound_array_buffer = 0;
            if (buffers[i] == g_bound_element_array_buffer) g_bound_element_array_buffer = 0;
        }
    }
    if (!pfn_glDeleteBuffers) pfn_glDeleteBuffers = (PFN_glDeleteBuffers)SDL_GL_GetProcAddress("glDeleteBuffers");
    if (pfn_glDeleteBuffers) pfn_glDeleteBuffers(n, buffers);
}

static void __cdecl wrap_glGenBuffers(GLsizei n, GLuint* buffers) {
    if (!pfn_glGenBuffers) pfn_glGenBuffers = (PFN_glGenBuffers)SDL_GL_GetProcAddress("glGenBuffers");
    if (pfn_glGenBuffers) pfn_glGenBuffers(n, buffers);
}

static void __cdecl wrap_glEnableVertexAttribArray(GLuint index) {
    if (index < 32) g_enabled_attrib_mask |= (1 << index);
    if (!pfn_glEnableVertexAttribArray) pfn_glEnableVertexAttribArray = (PFN_glEnableVertexAttribArray)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    if (pfn_glEnableVertexAttribArray) pfn_glEnableVertexAttribArray(index);
}

static void __cdecl wrap_glDisableVertexAttribArray(GLuint index) {
    if (index < 32) g_enabled_attrib_mask &= ~(1 << index);
    if (!pfn_glDisableVertexAttribArray) pfn_glDisableVertexAttribArray = (PFN_glDisableVertexAttribArray)SDL_GL_GetProcAddress("glDisableVertexAttribArray");
    if (pfn_glDisableVertexAttribArray) pfn_glDisableVertexAttribArray(index);
}

static void __cdecl wrap_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {
    if (!pfn_glVertexAttribPointer) pfn_glVertexAttribPointer = (PFN_glVertexAttribPointer)SDL_GL_GetProcAddress("glVertexAttribPointer");
    if (!pfn_glVertexAttribPointer) return;

    uintptr_t ptr_val = (uintptr_t)pointer;
    if (ptr_val >= 0x10000 && g_bound_array_buffer != 0) {
        if (pfn_glBindBuffer) pfn_glBindBuffer(GL_ARRAY_BUFFER, 0);
        pfn_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
        if (pfn_glBindBuffer) pfn_glBindBuffer(GL_ARRAY_BUFFER, g_bound_array_buffer);
        return;
    }
    pfn_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

static void __cdecl wrap_glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (!pfn_glVertexAttrib4f) pfn_glVertexAttrib4f = (PFN_glVertexAttrib4f)SDL_GL_GetProcAddress("glVertexAttrib4f");
    if (pfn_glVertexAttrib4f) pfn_glVertexAttrib4f(index, x, y, z, w);
}

static void __cdecl wrap_glUseProgram(GLuint program) {
    g_current_program = program;
    if (!pfn_glUseProgram) pfn_glUseProgram = (PFN_glUseProgram)SDL_GL_GetProcAddress("glUseProgram");
    if (pfn_glUseProgram) pfn_glUseProgram(program);
}

static void __cdecl wrap_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (!pfn_glUniformMatrix4fv) pfn_glUniformMatrix4fv = (PFN_glUniformMatrix4fv)SDL_GL_GetProcAddress("glUniformMatrix4fv");
    if (pfn_glUniformMatrix4fv) pfn_glUniformMatrix4fv(location, count, transpose, value);
}

static void __cdecl wrap_glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
    if (!pfn_glUniform4fv) pfn_glUniform4fv = (PFN_glUniform4fv)SDL_GL_GetProcAddress("glUniform4fv");
    if (pfn_glUniform4fv) pfn_glUniform4fv(location, count, value);
}

static void __cdecl wrap_glUniform1i(GLint location, GLint v0) {
    if (!pfn_glUniform1i) pfn_glUniform1i = (PFN_glUniform1i)SDL_GL_GetProcAddress("glUniform1i");
    if (pfn_glUniform1i) pfn_glUniform1i(location, v0);
}

static void __cdecl wrap_glUniform1f(GLint location, GLfloat v0) {
    if (!pfn_glUniform1f) pfn_glUniform1f = (PFN_glUniform1f)SDL_GL_GetProcAddress("glUniform1f");
    if (pfn_glUniform1f) pfn_glUniform1f(location, v0);
}

static void __cdecl wrap_glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
    if (!pfn_glUniform2fv) pfn_glUniform2fv = (PFN_glUniform2fv)SDL_GL_GetProcAddress("glUniform2fv");
    if (pfn_glUniform2fv) pfn_glUniform2fv(location, count, value);
}

static void __cdecl wrap_glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    if (!pfn_glUniform3fv) pfn_glUniform3fv = (PFN_glUniform3fv)SDL_GL_GetProcAddress("glUniform3fv");
    if (pfn_glUniform3fv) pfn_glUniform3fv(location, count, value);
}

static GLuint __cdecl wrap_glCreateProgram() {
    if (!pfn_glCreateProgram) pfn_glCreateProgram = (PFN_glCreateProgram)SDL_GL_GetProcAddress("glCreateProgram");
    return pfn_glCreateProgram ? pfn_glCreateProgram() : 0;
}

static void __cdecl wrap_glAttachShader(GLuint program, GLuint shader) {
    if (!pfn_glAttachShader) pfn_glAttachShader = (PFN_glAttachShader)SDL_GL_GetProcAddress("glAttachShader");
    if (pfn_glAttachShader) pfn_glAttachShader(program, shader);
}

static void __cdecl wrap_glLinkProgram(GLuint program) {
    if (!pfn_glLinkProgram) pfn_glLinkProgram = (PFN_glLinkProgram)SDL_GL_GetProcAddress("glLinkProgram");
    if (pfn_glLinkProgram) pfn_glLinkProgram(program);
}

static void __cdecl wrap_glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    if (!pfn_glGetProgramiv) pfn_glGetProgramiv = (PFN_glGetProgramiv)SDL_GL_GetProcAddress("glGetProgramiv");
    if (pfn_glGetProgramiv) pfn_glGetProgramiv(program, pname, params);
}

static void __cdecl wrap_glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if (!pfn_glGetProgramInfoLog) pfn_glGetProgramInfoLog = (PFN_glGetProgramInfoLog)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    if (pfn_glGetProgramInfoLog) pfn_glGetProgramInfoLog(program, bufSize, length, infoLog);
}

static GLint __cdecl wrap_glGetUniformLocation(GLuint program, const GLchar* name) {
    if (!pfn_glGetUniformLocation) pfn_glGetUniformLocation = (PFN_glGetUniformLocation)SDL_GL_GetProcAddress("glGetUniformLocation");
    return pfn_glGetUniformLocation ? pfn_glGetUniformLocation(program, name) : -1;
}

static void __cdecl wrap_glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    if (!pfn_glBindAttribLocation) pfn_glBindAttribLocation = (PFN_glBindAttribLocation)SDL_GL_GetProcAddress("glBindAttribLocation");
    if (pfn_glBindAttribLocation) pfn_glBindAttribLocation(program, index, name);
}

static void __cdecl wrap_glDepthRangef(GLclampf n, GLclampf f) {
    if (!pfn_glDepthRangef) pfn_glDepthRangef = (PFN_glDepthRangef)SDL_GL_GetProcAddress("glDepthRangef");
    if (pfn_glDepthRangef) pfn_glDepthRangef(n, f);
    else glDepthRange((double)n, (double)f);
}

static GLuint __cdecl wrap_glCreateShader(GLenum type) {
    if (!pfn_glCreateShader) pfn_glCreateShader = (PFN_glCreateShader)SDL_GL_GetProcAddress("glCreateShader");
    return pfn_glCreateShader ? pfn_glCreateShader(type) : 0;
}

static void __cdecl wrap_glCompileShader(GLuint shader) {
    if (!pfn_glCompileShader) pfn_glCompileShader = (PFN_glCompileShader)SDL_GL_GetProcAddress("glCompileShader");
    if (pfn_glCompileShader) pfn_glCompileShader(shader);
}

static void __cdecl wrap_glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    if (!pfn_glGetShaderiv) pfn_glGetShaderiv = (PFN_glGetShaderiv)SDL_GL_GetProcAddress("glGetShaderiv");
    if (pfn_glGetShaderiv) pfn_glGetShaderiv(shader, pname, params);
}

static void __cdecl wrap_glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if (!pfn_glGetShaderInfoLog) pfn_glGetShaderInfoLog = (PFN_glGetShaderInfoLog)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    if (pfn_glGetShaderInfoLog) pfn_glGetShaderInfoLog(shader, bufSize, length, infoLog);
}

static void __cdecl wrap_glDeleteShader(GLuint shader) {
    if (!pfn_glDeleteShader) pfn_glDeleteShader = (PFN_glDeleteShader)SDL_GL_GetProcAddress("glDeleteShader");
    if (pfn_glDeleteShader) pfn_glDeleteShader(shader);
}

// Shader sanitizer: strips GLES precision modifiers for desktop OpenGL
static void __cdecl diag_glShaderSource(uint32_t shader, int count, const char** string, const int* length) {
    if (!pfn_glShaderSource) pfn_glShaderSource = (PFN_glShaderSource)SDL_GL_GetProcAddress("glShaderSource");
    if (!pfn_glShaderSource) return;

    if (count > 0 && string && *string) {
        size_t total_len = 0;
        for (int i = 0; i < count; i++) {
            total_len += (length && length[i] > 0) ? length[i] : (string[i] ? strlen(string[i]) : 0);
        }

        char* combined = (char*)malloc(total_len + 1);
        combined[0] = '\0';
        for (int i = 0; i < count; i++) {
            if (!string[i]) continue;
            if (length && length[i] > 0) strncat(combined, string[i], length[i]);
            else strcat(combined, string[i]);
        }

        char* pos = combined;
        while ((pos = strstr(pos, "precision")) != nullptr) {
            char* semi = strchr(pos, ';');
            if (semi) {
                *pos = '/'; *(pos + 1) = '*'; *(semi - 1) = '*'; *semi = '/';
                pos = semi + 1; continue;
            }
            pos += 9;
        }

        static const char* compat_header = "#define lowp\n#define mediump\n#define highp\n";
        const char* final_strings[2] = { compat_header, combined };
        pfn_glShaderSource((GLuint)shader, 2, final_strings, nullptr);
        free(combined);
    } else {
        pfn_glShaderSource((GLuint)shader, (GLsizei)count, (const GLchar* const*)string, (const GLint*)length);
    }
}

// Viewport and Scissor Transformation
static void __cdecl wrap_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    float scale_x = (float)g_viewport.vp_w / GAME_CANVAS_W;
    float scale_y = (float)g_viewport.vp_h / GAME_CANVAS_H;
    glViewport(g_viewport.vp_x + (GLint)(x * scale_x),
               g_viewport.vp_y + (GLint)(y * scale_y),
               (GLsizei)(w * scale_x),
               (GLsizei)(h * scale_y));
}

static void __cdecl wrap_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    float scale_x = (float)g_viewport.vp_w / GAME_CANVAS_W;
    float scale_y = (float)g_viewport.vp_h / GAME_CANVAS_H;
    glScissor(g_viewport.vp_x + (GLint)(x * scale_x),
              g_viewport.vp_y + (GLint)(y * scale_y),
              (GLsizei)(width * scale_x),
              (GLsizei)(height * scale_y));
}

static void __cdecl wrap_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { glClearColor(r, g, b, a); }
static void __cdecl wrap_glClear(GLbitfield mask) { glClear(mask); }
static void __cdecl wrap_glEnable(GLenum cap) { glEnable(cap); }
static void __cdecl wrap_glDisable(GLenum cap) { glDisable(cap); }
static void __cdecl wrap_glDepthFunc(GLenum func) { glDepthFunc(func); }
static void __cdecl wrap_glDepthMask(GLboolean flag) { glDepthMask(flag); }
static void __cdecl wrap_glFrontFace(GLenum mode) { glFrontFace(mode); }
static GLenum __cdecl wrap_glGetError() { return glGetError(); }
static void __cdecl wrap_glBlendFunc(GLenum sfactor, GLenum dfactor) { glBlendFunc(sfactor, dfactor); }
static void __cdecl wrap_glBindTexture(GLenum target, GLuint texture) { glBindTexture(target, texture); }
static void __cdecl wrap_glDeleteTextures(GLsizei n, const GLuint* textures) { glDeleteTextures(n, textures); }
static void __cdecl wrap_glGenTextures(GLsizei n, GLuint* textures) { glGenTextures(n, textures); }
static void __cdecl wrap_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels) {
    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}
static void __cdecl wrap_glTexParameterf(GLenum target, GLenum pname, GLfloat param) { glTexParameterf(target, pname, param); }
static void __cdecl wrap_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { glColorMask(red, green, blue, alpha); }
static const GLubyte* __cdecl wrap_glGetString(GLenum name) { return glGetString(name); }
static void __cdecl wrap_glCullFace(GLenum mode) { glCullFace(mode); }
static void __cdecl wrap_glPixelStorei(GLenum pname, GLint param) { glPixelStorei(pname, param); }
static void __cdecl wrap_glLineWidth(GLfloat width) { glLineWidth(width); }

// --- Active Shader Attribute Masking (NVIDIA Out-of-Bounds Fetch Fix) ---
static uint32_t get_active_shader_attrib_mask(GLuint program) {
    if (!program) return 0xFFFFFFFF;
    if (!pfn_glGetProgramiv) pfn_glGetProgramiv = (PFN_glGetProgramiv)SDL_GL_GetProcAddress("glGetProgramiv");
    if (!pfn_glGetActiveAttrib) pfn_glGetActiveAttrib = (PFN_glGetActiveAttrib)SDL_GL_GetProcAddress("glGetActiveAttrib");
    if (!pfn_glGetAttribLocation) pfn_glGetAttribLocation = (PFN_glGetAttribLocation)SDL_GL_GetProcAddress("glGetAttribLocation");

    if (!pfn_glGetProgramiv || !pfn_glGetActiveAttrib || !pfn_glGetAttribLocation) {
        return 0xFFFFFFFF;
    }

    GLint active_count = 0;
    pfn_glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &active_count);
    if (active_count <= 0) return 0xFFFFFFFF;

    uint32_t mask = 0;
    char name_buf[128];
    for (GLint i = 0; i < active_count; i++) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        pfn_glGetActiveAttrib(program, (GLuint)i, sizeof(name_buf), &length, &size, &type, name_buf);
        GLint loc = pfn_glGetAttribLocation(program, name_buf);
        if (loc >= 0 && loc < 32) {
            mask |= (1 << loc);
        }
    }
    return mask;
}

static void __cdecl wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    uintptr_t ptr_val = (uintptr_t)indices;

    int rebound_element = 0;
    if (ptr_val >= 0x10000 && g_bound_element_array_buffer != 0) {
        if (pfn_glBindBuffer) pfn_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        rebound_element = 1;
    }

    uint32_t needed_mask = get_active_shader_attrib_mask(g_current_program);
    uint32_t orphaned_mask = g_enabled_attrib_mask & ~needed_mask;

    if (orphaned_mask != 0 && pfn_glDisableVertexAttribArray) {
        for (int i = 0; i < 16; i++) {
            if (orphaned_mask & (1 << i)) {
                pfn_glDisableVertexAttribArray(i);
            }
        }
    }

    glDrawElements(mode, count, type, indices);

    if (orphaned_mask != 0 && pfn_glEnableVertexAttribArray) {
        for (int i = 0; i < 16; i++) {
            if (orphaned_mask & (1 << i)) {
                pfn_glEnableVertexAttribArray(i);
            }
        }
    }

    if (rebound_element && pfn_glBindBuffer) {
        pfn_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_bound_element_array_buffer);
    }
}

static void __cdecl wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    uint32_t needed_mask = get_active_shader_attrib_mask(g_current_program);
    uint32_t orphaned_mask = g_enabled_attrib_mask & ~needed_mask;

    if (orphaned_mask != 0 && pfn_glDisableVertexAttribArray) {
        for (int i = 0; i < 16; i++) {
            if (orphaned_mask & (1 << i)) {
                pfn_glDisableVertexAttribArray(i);
            }
        }
    }

    glDrawArrays(mode, first, count);

    if (orphaned_mask != 0 && pfn_glEnableVertexAttribArray) {
        for (int i = 0; i < 16; i++) {
            if (orphaned_mask & (1 << i)) {
                pfn_glEnableVertexAttribArray(i);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Initialization & Symbol Registration
// -----------------------------------------------------------------------------
extern "C" void init_gl_wrappers() {
    REGISTER_SYMBOL_HOOK("glActiveTexture", wrap_glActiveTexture);
    REGISTER_SYMBOL_HOOK("glBindBuffer", wrap_glBindBuffer);
    REGISTER_SYMBOL_HOOK("glBufferData", wrap_glBufferData);
    REGISTER_SYMBOL_HOOK("glDeleteBuffers", wrap_glDeleteBuffers);
    REGISTER_SYMBOL_HOOK("glGenBuffers", wrap_glGenBuffers);
    REGISTER_SYMBOL_HOOK("glEnableVertexAttribArray", wrap_glEnableVertexAttribArray);
    REGISTER_SYMBOL_HOOK("glDisableVertexAttribArray", wrap_glDisableVertexAttribArray);
    REGISTER_SYMBOL_HOOK("glVertexAttribPointer", wrap_glVertexAttribPointer);
    REGISTER_SYMBOL_HOOK("glVertexAttrib4f", wrap_glVertexAttrib4f);
    REGISTER_SYMBOL_HOOK("glUseProgram", wrap_glUseProgram);
    REGISTER_SYMBOL_HOOK("glUniformMatrix4fv", wrap_glUniformMatrix4fv);
    REGISTER_SYMBOL_HOOK("glUniform4fv", wrap_glUniform4fv);
    REGISTER_SYMBOL_HOOK("glUniform1i", wrap_glUniform1i);
    REGISTER_SYMBOL_HOOK("glUniform1f", wrap_glUniform1f);
    REGISTER_SYMBOL_HOOK("glUniform2fv", wrap_glUniform2fv);
    REGISTER_SYMBOL_HOOK("glUniform3fv", wrap_glUniform3fv);
    REGISTER_SYMBOL_HOOK("glCreateProgram", wrap_glCreateProgram);
    REGISTER_SYMBOL_HOOK("glAttachShader", wrap_glAttachShader);
    REGISTER_SYMBOL_HOOK("glLinkProgram", wrap_glLinkProgram);
    REGISTER_SYMBOL_HOOK("glGetProgramiv", wrap_glGetProgramiv);
    REGISTER_SYMBOL_HOOK("glGetProgramInfoLog", wrap_glGetProgramInfoLog);
    REGISTER_SYMBOL_HOOK("glGetUniformLocation", wrap_glGetUniformLocation);
    REGISTER_SYMBOL_HOOK("glBindAttribLocation", wrap_glBindAttribLocation);
    REGISTER_SYMBOL_HOOK("glDepthRangef", wrap_glDepthRangef);
    REGISTER_SYMBOL_HOOK("glCreateShader", wrap_glCreateShader);
    REGISTER_SYMBOL_HOOK("glShaderSource", diag_glShaderSource);
    REGISTER_SYMBOL_HOOK("glCompileShader", wrap_glCompileShader);
    REGISTER_SYMBOL_HOOK("glGetShaderiv", wrap_glGetShaderiv);
    REGISTER_SYMBOL_HOOK("glGetShaderInfoLog", wrap_glGetShaderInfoLog);
    REGISTER_SYMBOL_HOOK("glDeleteShader", wrap_glDeleteShader);

    REGISTER_SYMBOL_HOOK("glViewport", wrap_glViewport);
    REGISTER_SYMBOL_HOOK("glScissor", wrap_glScissor);
    REGISTER_SYMBOL_HOOK("glClearColor", wrap_glClearColor);
    REGISTER_SYMBOL_HOOK("glClear", wrap_glClear);
    REGISTER_SYMBOL_HOOK("glEnable", wrap_glEnable);
    REGISTER_SYMBOL_HOOK("glDisable", wrap_glDisable);
    REGISTER_SYMBOL_HOOK("glDepthFunc", wrap_glDepthFunc);
    REGISTER_SYMBOL_HOOK("glDepthMask", wrap_glDepthMask);
    REGISTER_SYMBOL_HOOK("glFrontFace", wrap_glFrontFace);
    REGISTER_SYMBOL_HOOK("glGetError", wrap_glGetError);
    REGISTER_SYMBOL_HOOK("glBlendFunc", wrap_glBlendFunc);
    REGISTER_SYMBOL_HOOK("glBindTexture", wrap_glBindTexture);
    REGISTER_SYMBOL_HOOK("glDeleteTextures", wrap_glDeleteTextures);
    REGISTER_SYMBOL_HOOK("glGenTextures", wrap_glGenTextures);
    REGISTER_SYMBOL_HOOK("glTexImage2D", wrap_glTexImage2D);
    REGISTER_SYMBOL_HOOK("glTexParameterf", wrap_glTexParameterf);
    REGISTER_SYMBOL_HOOK("glColorMask", wrap_glColorMask);
    REGISTER_SYMBOL_HOOK("glGetString", wrap_glGetString);
    REGISTER_SYMBOL_HOOK("glCullFace", wrap_glCullFace);
    REGISTER_SYMBOL_HOOK("glPixelStorei", wrap_glPixelStorei);
    REGISTER_SYMBOL_HOOK("glLineWidth", wrap_glLineWidth);
    REGISTER_SYMBOL_HOOK("glDrawArrays", wrap_glDrawArrays);
    REGISTER_SYMBOL_HOOK("glDrawElements", wrap_glDrawElements);
}