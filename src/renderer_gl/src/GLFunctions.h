#pragma once

#include "cadly/renderer_gl/GLRenderer.h"  // GLLoadProc

// Khronos's core ARB header. It defines the GL types, all GL 4.1 constants,
// and `PFNGL*PROC` typedefs we use below. We do NOT define
// GL_GLEXT_PROTOTYPES so the header never declares actual symbols — we resolve
// every entry point through the host's loader at runtime.
#include <GL/glcorearb.h>

namespace cadly::renderer_gl::detail {

// Function-pointer table covering exactly the subset of GL 4.1 core that the
// renderer uses. Populated through a host-supplied `GLLoadProc`. Replaces
// QOpenGLFunctions_4_1_Core, which was the sole reason this module had to link
// against Qt6.
//
// Field names match the GL function names so call sites read `gl.glClear(...)`
// just like Qt's wrapper did — the conversion was mostly mechanical.
struct GLFunctions {
  PFNGLACTIVETEXTUREPROC           glActiveTexture           = nullptr;
  PFNGLATTACHSHADERPROC            glAttachShader            = nullptr;
  PFNGLBINDBUFFERPROC              glBindBuffer              = nullptr;
  PFNGLBINDBUFFERBASEPROC          glBindBufferBase          = nullptr;
  PFNGLBINDFRAMEBUFFERPROC         glBindFramebuffer         = nullptr;
  PFNGLBINDRENDERBUFFERPROC        glBindRenderbuffer        = nullptr;
  PFNGLBINDTEXTUREPROC             glBindTexture             = nullptr;
  PFNGLBINDVERTEXARRAYPROC         glBindVertexArray         = nullptr;
  PFNGLBLENDFUNCPROC               glBlendFunc               = nullptr;
  PFNGLBLENDFUNCSEPARATEPROC       glBlendFuncSeparate       = nullptr;
  PFNGLBLITFRAMEBUFFERPROC         glBlitFramebuffer         = nullptr;
  PFNGLBUFFERDATAPROC              glBufferData              = nullptr;
  PFNGLBUFFERSUBDATAPROC           glBufferSubData           = nullptr;
  PFNGLCHECKFRAMEBUFFERSTATUSPROC  glCheckFramebufferStatus  = nullptr;
  PFNGLCLEARPROC                   glClear                   = nullptr;
  PFNGLCLEARCOLORPROC              glClearColor              = nullptr;
  PFNGLCLEARDEPTHFPROC             glClearDepthf             = nullptr;
  PFNGLCOMPILESHADERPROC           glCompileShader           = nullptr;
  PFNGLCREATEPROGRAMPROC           glCreateProgram           = nullptr;
  PFNGLCREATESHADERPROC            glCreateShader            = nullptr;
  PFNGLCULLFACEPROC                glCullFace                = nullptr;
  PFNGLDELETEBUFFERSPROC           glDeleteBuffers           = nullptr;
  PFNGLDELETEFRAMEBUFFERSPROC      glDeleteFramebuffers      = nullptr;
  PFNGLDELETEPROGRAMPROC           glDeleteProgram           = nullptr;
  PFNGLDELETERENDERBUFFERSPROC     glDeleteRenderbuffers     = nullptr;
  PFNGLDELETESHADERPROC            glDeleteShader            = nullptr;
  PFNGLDELETETEXTURESPROC          glDeleteTextures          = nullptr;
  PFNGLDELETEVERTEXARRAYSPROC      glDeleteVertexArrays      = nullptr;
  PFNGLDEPTHFUNCPROC               glDepthFunc               = nullptr;
  PFNGLDEPTHMASKPROC               glDepthMask               = nullptr;
  PFNGLDISABLEPROC                 glDisable                 = nullptr;
  PFNGLDRAWARRAYSPROC              glDrawArrays              = nullptr;
  PFNGLDRAWELEMENTSPROC            glDrawElements            = nullptr;
  PFNGLENABLEPROC                  glEnable                  = nullptr;
  PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
  PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;
  PFNGLFRAMEBUFFERTEXTURE2DPROC    glFramebufferTexture2D    = nullptr;
  PFNGLFRONTFACEPROC               glFrontFace               = nullptr;
  PFNGLGENBUFFERSPROC              glGenBuffers              = nullptr;
  PFNGLGENFRAMEBUFFERSPROC         glGenFramebuffers         = nullptr;
  PFNGLGENRENDERBUFFERSPROC        glGenRenderbuffers        = nullptr;
  PFNGLGENTEXTURESPROC             glGenTextures             = nullptr;
  PFNGLGENVERTEXARRAYSPROC         glGenVertexArrays         = nullptr;
  PFNGLGENERATEMIPMAPPROC          glGenerateMipmap          = nullptr;
  PFNGLGETINTEGERVPROC             glGetIntegerv             = nullptr;
  PFNGLGETPROGRAMINFOLOGPROC       glGetProgramInfoLog       = nullptr;
  PFNGLGETPROGRAMIVPROC            glGetProgramiv            = nullptr;
  PFNGLGETSHADERINFOLOGPROC        glGetShaderInfoLog        = nullptr;
  PFNGLGETSHADERIVPROC             glGetShaderiv             = nullptr;
  PFNGLGETSTRINGPROC               glGetString               = nullptr;
  PFNGLGETUNIFORMBLOCKINDEXPROC    glGetUniformBlockIndex    = nullptr;
  PFNGLGETUNIFORMLOCATIONPROC      glGetUniformLocation      = nullptr;
  PFNGLLINEWIDTHPROC               glLineWidth               = nullptr;
  PFNGLLINKPROGRAMPROC             glLinkProgram             = nullptr;
  PFNGLPIXELSTOREIPROC             glPixelStorei             = nullptr;
  PFNGLPOLYGONMODEPROC             glPolygonMode             = nullptr;
  PFNGLPOLYGONOFFSETPROC           glPolygonOffset           = nullptr;
  PFNGLPRIMITIVERESTARTINDEXPROC   glPrimitiveRestartIndex   = nullptr;
  PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample = nullptr;
  PFNGLSHADERSOURCEPROC            glShaderSource            = nullptr;
  PFNGLTEXIMAGE2DPROC              glTexImage2D              = nullptr;
  PFNGLTEXPARAMETERIPROC           glTexParameteri           = nullptr;
  PFNGLUNIFORM1FPROC               glUniform1f               = nullptr;
  PFNGLUNIFORM1IPROC               glUniform1i               = nullptr;
  PFNGLUNIFORM2FVPROC              glUniform2fv              = nullptr;
  PFNGLUNIFORM3FVPROC              glUniform3fv              = nullptr;
  PFNGLUNIFORM4FVPROC              glUniform4fv              = nullptr;
  PFNGLUNIFORMBLOCKBINDINGPROC     glUniformBlockBinding     = nullptr;
  PFNGLUNIFORMMATRIX3FVPROC        glUniformMatrix3fv        = nullptr;
  PFNGLUNIFORMMATRIX4FVPROC        glUniformMatrix4fv        = nullptr;
  PFNGLUSEPROGRAMPROC              glUseProgram              = nullptr;
  PFNGLVERTEXATTRIBPOINTERPROC     glVertexAttribPointer     = nullptr;
  PFNGLVIEWPORTPROC                glViewport                = nullptr;

  // Resolves every member through `proc`. Returns true if all entry points
  // were found; on failure, leaves the partially-populated table behind and
  // logs the first missing name so the host can diagnose the GL version
  // mismatch (e.g. the context was created with < 4.1, or core profile was
  // not requested).
  bool load(const GLLoadProc& proc);
};

} // namespace cadly::renderer_gl::detail
