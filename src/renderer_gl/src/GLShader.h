#pragma once

#include <QOpenGLFunctions_4_1_Core>

#include <optional>
#include <string>
#include <unordered_map>

namespace cadly::renderer_gl::detail {

// Tiny shader/program wrapper. Resolves source from the shader asset directory
// at load time, compiles, links, and caches uniform locations. Does NOT own
// any GL state beyond the program id.
class GLProgram {
public:
  GLProgram() = default;
  GLProgram(const GLProgram&) = delete;
  GLProgram& operator=(const GLProgram&) = delete;

  // Compile + link from raw source. Returns false on failure; the error log
  // is forwarded to the platform logger and exposed via last_error().
  bool build(QOpenGLFunctions_4_1_Core& gl,
             const std::string& vertex_source,
             const std::string& fragment_source,
             const char* debug_name);

  void destroy(QOpenGLFunctions_4_1_Core& gl);

  GLuint id() const { return id_; }
  bool   valid() const { return id_ != 0; }
  const std::string& last_error() const { return last_error_; }

  GLint  uniform(QOpenGLFunctions_4_1_Core& gl, const char* name);
  GLuint uniform_block(QOpenGLFunctions_4_1_Core& gl, const char* name);

private:
  GLuint id_{0};
  std::string last_error_;
  std::unordered_map<std::string, GLint>  uniform_cache_;
  std::unordered_map<std::string, GLuint> block_cache_;
};

// Look up a shader source file in the runtime asset tree.
std::optional<std::string> load_shader_source(const std::string& filename);

} // namespace cadly::renderer_gl::detail
