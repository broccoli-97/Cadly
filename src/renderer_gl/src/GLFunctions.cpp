#include "GLFunctions.h"

#include "cadly/platform/Log.h"

namespace cadly::renderer_gl::detail {

bool GLFunctions::load(const GLLoadProc& proc) {
  if (!proc) {
    CADLY_LOG_ERROR("GL function loader is null; cannot resolve GL entry points.");
    return false;
  }

  bool ok = true;

  // Resolve a single entry point and assign it to the matching struct member.
  // The reinterpret_cast from `void*` to a function-pointer type is
  // implementation-defined but works on every platform with a working dlsym /
  // wglGetProcAddress / glXGetProcAddress flavour — i.e. every platform the
  // renderer is expected to run on.
  #define CADLY_LOAD(field) do {                                              \
      void* sym = proc(#field);                                               \
      field = reinterpret_cast<decltype(field)>(sym);                         \
      if (!field) {                                                           \
        if (ok) {                                                             \
          CADLY_LOG_ERROR(                                                    \
            "Failed to load GL entry point '{}'. Is the GL 4.1 core "         \
            "context current?", #field);                                      \
        }                                                                    \
        ok = false;                                                           \
      }                                                                       \
    } while (0)

  CADLY_LOAD(glActiveTexture);
  CADLY_LOAD(glAttachShader);
  CADLY_LOAD(glBindBuffer);
  CADLY_LOAD(glBindBufferBase);
  CADLY_LOAD(glBindFramebuffer);
  CADLY_LOAD(glBindRenderbuffer);
  CADLY_LOAD(glBindTexture);
  CADLY_LOAD(glBindVertexArray);
  CADLY_LOAD(glBlendFunc);
  CADLY_LOAD(glBlitFramebuffer);
  CADLY_LOAD(glBufferData);
  CADLY_LOAD(glBufferSubData);
  CADLY_LOAD(glCheckFramebufferStatus);
  CADLY_LOAD(glClear);
  CADLY_LOAD(glClearDepthf);
  CADLY_LOAD(glCompileShader);
  CADLY_LOAD(glCreateProgram);
  CADLY_LOAD(glCreateShader);
  CADLY_LOAD(glCullFace);
  CADLY_LOAD(glDeleteBuffers);
  CADLY_LOAD(glDeleteFramebuffers);
  CADLY_LOAD(glDeleteProgram);
  CADLY_LOAD(glDeleteRenderbuffers);
  CADLY_LOAD(glDeleteShader);
  CADLY_LOAD(glDeleteTextures);
  CADLY_LOAD(glDeleteVertexArrays);
  CADLY_LOAD(glDepthFunc);
  CADLY_LOAD(glDepthMask);
  CADLY_LOAD(glDisable);
  CADLY_LOAD(glDrawArrays);
  CADLY_LOAD(glDrawElements);
  CADLY_LOAD(glEnable);
  CADLY_LOAD(glEnableVertexAttribArray);
  CADLY_LOAD(glFramebufferRenderbuffer);
  CADLY_LOAD(glFramebufferTexture2D);
  CADLY_LOAD(glFrontFace);
  CADLY_LOAD(glGenBuffers);
  CADLY_LOAD(glGenFramebuffers);
  CADLY_LOAD(glGenRenderbuffers);
  CADLY_LOAD(glGenTextures);
  CADLY_LOAD(glGenVertexArrays);
  CADLY_LOAD(glGenerateMipmap);
  CADLY_LOAD(glGetIntegerv);
  CADLY_LOAD(glGetProgramInfoLog);
  CADLY_LOAD(glGetProgramiv);
  CADLY_LOAD(glGetShaderInfoLog);
  CADLY_LOAD(glGetShaderiv);
  CADLY_LOAD(glGetString);
  CADLY_LOAD(glGetUniformBlockIndex);
  CADLY_LOAD(glGetUniformLocation);
  CADLY_LOAD(glLineWidth);
  CADLY_LOAD(glLinkProgram);
  CADLY_LOAD(glPixelStorei);
  CADLY_LOAD(glPolygonMode);
  CADLY_LOAD(glPolygonOffset);
  CADLY_LOAD(glPrimitiveRestartIndex);
  CADLY_LOAD(glRenderbufferStorageMultisample);
  CADLY_LOAD(glShaderSource);
  CADLY_LOAD(glTexImage2D);
  CADLY_LOAD(glTexParameteri);
  CADLY_LOAD(glUniform1f);
  CADLY_LOAD(glUniform1i);
  CADLY_LOAD(glUniform2fv);
  CADLY_LOAD(glUniform3fv);
  CADLY_LOAD(glUniform4fv);
  CADLY_LOAD(glUniformBlockBinding);
  CADLY_LOAD(glUniformMatrix3fv);
  CADLY_LOAD(glUniformMatrix4fv);
  CADLY_LOAD(glUseProgram);
  CADLY_LOAD(glVertexAttribPointer);
  CADLY_LOAD(glViewport);

  #undef CADLY_LOAD
  return ok;
}

} // namespace cadly::renderer_gl::detail
