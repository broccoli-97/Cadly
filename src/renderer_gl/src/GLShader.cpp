#include "GLShader.h"

#include "cadly/platform/Log.h"
#include "cadly/platform/Paths.h"

#include <vector>

namespace cadly::renderer_gl::detail {

namespace {

GLuint compile_stage(QOpenGLFunctions_4_1_Core& gl,
                     GLenum stage,
                     const std::string& source,
                     std::string& error_out) {
  GLuint shader = gl.glCreateShader(stage);
  const char* src = source.c_str();
  const GLint len = static_cast<GLint>(source.size());
  gl.glShaderSource(shader, 1, &src, &len);
  gl.glCompileShader(shader);

  GLint status = GL_FALSE;
  gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint log_len = 0;
    gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
    std::vector<char> log(static_cast<std::size_t>(log_len) + 1, 0);
    gl.glGetShaderInfoLog(shader, log_len, nullptr, log.data());
    error_out.assign(log.data());
    gl.glDeleteShader(shader);
    return 0;
  }
  return shader;
}

} // namespace

bool GLProgram::build(QOpenGLFunctions_4_1_Core& gl,
                      const std::string& vertex_source,
                      const std::string& fragment_source,
                      const char* debug_name) {
  destroy(gl);

  std::string err;
  GLuint vs = compile_stage(gl, GL_VERTEX_SHADER, vertex_source, err);
  if (!vs) {
    last_error_ = std::string("vertex (") + debug_name + "): " + err;
    CADLY_LOG_ERROR("{}", last_error_);
    return false;
  }
  GLuint fs = compile_stage(gl, GL_FRAGMENT_SHADER, fragment_source, err);
  if (!fs) {
    last_error_ = std::string("fragment (") + debug_name + "): " + err;
    CADLY_LOG_ERROR("{}", last_error_);
    gl.glDeleteShader(vs);
    return false;
  }

  id_ = gl.glCreateProgram();
  gl.glAttachShader(id_, vs);
  gl.glAttachShader(id_, fs);
  gl.glLinkProgram(id_);

  GLint status = GL_FALSE;
  gl.glGetProgramiv(id_, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    GLint log_len = 0;
    gl.glGetProgramiv(id_, GL_INFO_LOG_LENGTH, &log_len);
    std::vector<char> log(static_cast<std::size_t>(log_len) + 1, 0);
    gl.glGetProgramInfoLog(id_, log_len, nullptr, log.data());
    last_error_ = std::string("link (") + debug_name + "): " + log.data();
    CADLY_LOG_ERROR("{}", last_error_);
    gl.glDeleteProgram(id_);
    id_ = 0;
  }
  gl.glDeleteShader(vs);
  gl.glDeleteShader(fs);
  return id_ != 0;
}

void GLProgram::destroy(QOpenGLFunctions_4_1_Core& gl) {
  if (id_) {
    gl.glDeleteProgram(id_);
    id_ = 0;
  }
  uniform_cache_.clear();
  block_cache_.clear();
  last_error_.clear();
}

GLint GLProgram::uniform(QOpenGLFunctions_4_1_Core& gl, const char* name) {
  auto it = uniform_cache_.find(name);
  if (it != uniform_cache_.end()) return it->second;
  GLint loc = gl.glGetUniformLocation(id_, name);
  uniform_cache_.emplace(name, loc);
  return loc;
}

GLuint GLProgram::uniform_block(QOpenGLFunctions_4_1_Core& gl, const char* name) {
  auto it = block_cache_.find(name);
  if (it != block_cache_.end()) return it->second;
  GLuint idx = gl.glGetUniformBlockIndex(id_, name);
  block_cache_.emplace(name, idx);
  return idx;
}

std::optional<std::string> load_shader_source(const std::string& filename) {
  auto dir = cadly::platform::find_asset_dir("shaders/glsl");
  if (!dir) return std::nullopt;
  const auto path = *dir / filename;
  auto contents = cadly::platform::read_text_file(path);
  if (!contents) {
    CADLY_LOG_ERROR("Could not read shader source: {}", path.string());
  }
  return contents;
}

} // namespace cadly::renderer_gl::detail
