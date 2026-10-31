#include "rensga/render/gl.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "rensga/core/log.h"

namespace rensga {

namespace {

using GLuint = unsigned;
using GLint = int;
using GLenum = unsigned;
using GLsizei = int;
using GLsizeiptr = ptrdiff_t;

#if defined(_WIN32)
#define RG_GLAPI __stdcall
#else
#define RG_GLAPI
#endif

// Constants (values from the GL / GLES headers; kept local so no GL header is needed).
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x100, GL_TRIANGLES = 4, GL_UNSIGNED_BYTE = 0x1401,
                 GL_FLOAT = 0x1406, GL_TEXTURE_2D = 0x0DE1, GL_RGBA = 0x1908, GL_RGBA8 = 0x8058,
                 GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802,
                 GL_TEXTURE_WRAP_T = 0x2803, GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_CLAMP_TO_EDGE = 0x812F,
                 GL_DEPTH_TEST = 0x0B71, GL_BLEND = 0x0BE2, GL_CULL_FACE = 0x0B44, GL_SRC_ALPHA = 0x0302,
                 GL_ONE_MINUS_SRC_ALPHA = 0x0303, GL_LEQUAL = 0x0203, GL_ARRAY_BUFFER = 0x8892,
                 GL_STATIC_DRAW = 0x88E4, GL_STREAM_DRAW = 0x88E0, GL_VERTEX_SHADER = 0x8B31,
                 GL_FRAGMENT_SHADER = 0x8B30, GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82,
                 GL_FRAMEBUFFER = 0x8D40, GL_RENDERBUFFER = 0x8D41, GL_COLOR_ATTACHMENT0 = 0x8CE0,
                 GL_DEPTH_ATTACHMENT = 0x8D00, GL_DEPTH_COMPONENT24 = 0x81A6, GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
                 GL_TEXTURE0 = 0x84C0, GL_UNPACK_ALIGNMENT = 0x0CF5, GL_VERSION = 0x1F02, GL_RENDERER = 0x1F01,
                 GL_SCISSOR_TEST = 0x0C11;

#define RG_GL_FUNCS(X)                                                                     \
  X(void, ClearColor, (float, float, float, float))                                       \
  X(void, Clear, (GLenum))                                                                 \
  X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                      \
  X(void, Enable, (GLenum))                                                                \
  X(void, Disable, (GLenum))                                                               \
  X(void, DepthFunc, (GLenum))                                                             \
  X(void, BlendFunc, (GLenum, GLenum))                                                     \
  X(void, GenTextures, (GLsizei, GLuint*))                                                 \
  X(void, DeleteTextures, (GLsizei, const GLuint*))                                        \
  X(void, BindTexture, (GLenum, GLuint))                                                   \
  X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
  X(void, TexParameteri, (GLenum, GLenum, GLint))                                          \
  X(void, PixelStorei, (GLenum, GLint))                                                    \
  X(void, ActiveTexture, (GLenum))                                                         \
  X(void, GenBuffers, (GLsizei, GLuint*))                                                  \
  X(void, DeleteBuffers, (GLsizei, const GLuint*))                                         \
  X(void, BindBuffer, (GLenum, GLuint))                                                    \
  X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                           \
  X(void, BufferSubData, (GLenum, GLsizeiptr, GLsizeiptr, const void*))                    \
  X(void, GenVertexArrays, (GLsizei, GLuint*))                                             \
  X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                    \
  X(void, BindVertexArray, (GLuint))                                                       \
  X(void, EnableVertexAttribArray, (GLuint))                                               \
  X(void, VertexAttribPointer, (GLuint, GLint, GLenum, unsigned char, GLsizei, const void*)) \
  X(void, DrawArrays, (GLenum, GLint, GLsizei))                                            \
  X(GLuint, CreateShader, (GLenum))                                                        \
  X(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*))               \
  X(void, CompileShader, (GLuint))                                                         \
  X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                           \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*))                            \
  X(void, DeleteShader, (GLuint))                                                          \
  X(GLuint, CreateProgram, (void))                                                         \
  X(void, AttachShader, (GLuint, GLuint))                                                  \
  X(void, LinkProgram, (GLuint))                                                           \
  X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                          \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*))                           \
  X(void, UseProgram, (GLuint))                                                            \
  X(void, DeleteProgram, (GLuint))                                                         \
  X(GLint, GetUniformLocation, (GLuint, const char*))                                      \
  X(void, Uniform1i, (GLint, GLint))                                                       \
  X(void, Uniform2f, (GLint, float, float))                                                \
  X(void, UniformMatrix4fv, (GLint, GLsizei, unsigned char, const float*))                 \
  X(void, GenFramebuffers, (GLsizei, GLuint*))                                             \
  X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                    \
  X(void, BindFramebuffer, (GLenum, GLuint))                                               \
  X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                   \
  X(void, GenRenderbuffers, (GLsizei, GLuint*))                                            \
  X(void, DeleteRenderbuffers, (GLsizei, const GLuint*))                                   \
  X(void, BindRenderbuffer, (GLenum, GLuint))                                              \
  X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                         \
  X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                       \
  X(GLenum, CheckFramebufferStatus, (GLenum))                                              \
  X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))             \
  X(const unsigned char*, GetString, (GLenum))

struct GlApi {
#define X(ret, name, args) ret(RG_GLAPI* name) args = nullptr;
  RG_GL_FUNCS(X)
#undef X
};
GlApi gl;  // one GL context per process

bool loadGl() {
  bool ok = true;
#define X(ret, name, args)                                                                  \
  gl.name = reinterpret_cast<decltype(gl.name)>(SDL_GL_GetProcAddress("gl" #name));        \
  if (!gl.name) {                                                                           \
    RLOG_ERROR("gl", "missing GL entry point gl" #name);                                   \
    ok = false;                                                                             \
  }
  RG_GL_FUNCS(X)
#undef X
  return ok;
}

// One shader source for desktop GL 3.3 core and GLES 3.0: only the header differs.
#if defined(__ANDROID__)
const char* kHeader = "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\n";
#else
const char* kHeader = "#version 330 core\n";
#endif

const char* kMeshVs = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aCol;
uniform mat4 uMVP;
out vec4 vCol;
void main(){ vCol = aCol; gl_Position = uMVP * vec4(aPos, 1.0); })";
const char* kMeshFs = R"(
in vec4 vCol; out vec4 o;
void main(){ o = vec4(vCol.rgb, 1.0); })";

const char* kSpriteVs = R"(
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform vec2 uInvVirt;
out vec2 vUV; out vec4 vCol;
void main(){
  vUV = aUV; vCol = aCol;
  gl_Position = vec4(aPos.x * uInvVirt.x * 2.0 - 1.0, 1.0 - aPos.y * uInvVirt.y * 2.0, 0.0, 1.0);
})";
const char* kSpriteFs = R"(
in vec2 vUV; in vec4 vCol; out vec4 o;
uniform sampler2D uTex;
void main(){ o = texture(uTex, vUV) * vCol; })";

const char* kPresentVs = R"(
out vec2 vUV;
void main(){
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUV = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
const char* kPresentFs = R"(
in vec2 vUV; out vec4 o;
uniform sampler2D uTex;
void main(){ o = vec4(texture(uTex, vUV).rgb, 1.0); })";

}  // namespace

GlRenderer::~GlRenderer() { shutdown(); }

void GlRenderer::shutdown() {
  if (!ready_) return;
  ready_ = false;
  destroyTarget();
  for (u32 i = 0; i < texCount_; ++i) gl.DeleteTextures(1, &tex_[i].id);
  gl.DeleteBuffers(1, &vboMesh_);
  gl.DeleteBuffers(1, &vboSprite_);
  gl.DeleteVertexArrays(1, &vaoMesh_);
  gl.DeleteVertexArrays(1, &vaoSprite_);
  gl.DeleteVertexArrays(1, &vaoEmpty_);
  gl.DeleteProgram(progMesh_);
  gl.DeleteProgram(progSprite_);
  gl.DeleteProgram(progPresent_);
}

bool GlRenderer::compile(u32& program, const char* vs, const char* fs) {
  char log[512];
  GLuint sh[2];
  const char* src[2] = {vs, fs};
  const GLenum kinds[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
  for (int i = 0; i < 2; ++i) {
    const char* parts[2] = {kHeader, src[i]};
    sh[i] = gl.CreateShader(kinds[i]);
    gl.ShaderSource(sh[i], 2, parts, nullptr);
    gl.CompileShader(sh[i]);
    GLint ok = 0;
    gl.GetShaderiv(sh[i], GL_COMPILE_STATUS, &ok);
    if (!ok) {
      gl.GetShaderInfoLog(sh[i], sizeof(log), nullptr, log);
      RLOG_ERROR("gl", "shader compile failed: %s", log);
      return false;
    }
  }
  program = gl.CreateProgram();
  gl.AttachShader(program, sh[0]);
  gl.AttachShader(program, sh[1]);
  gl.LinkProgram(program);
  gl.DeleteShader(sh[0]);
  gl.DeleteShader(sh[1]);
  GLint ok = 0;
  gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
    RLOG_ERROR("gl", "program link failed: %s", log);
    return false;
  }
  return true;
}

bool GlRenderer::createTarget() {
  gl.GenTextures(1, &colorTex_);
  gl.BindTexture(GL_TEXTURE_2D, colorTex_);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(w_), static_cast<GLsizei>(h_), 0, GL_RGBA,
                GL_UNSIGNED_BYTE, nullptr);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.GenRenderbuffers(1, &depthRb_);
  gl.BindRenderbuffer(GL_RENDERBUFFER, depthRb_);
  gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(w_), static_cast<GLsizei>(h_));
  gl.GenFramebuffers(1, &fbo_);
  gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb_);
  const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!ok) RLOG_ERROR("gl", "framebuffer %ux%u incomplete", w_, h_);
  return ok;
}

void GlRenderer::destroyTarget() {
  if (fbo_) gl.DeleteFramebuffers(1, &fbo_);
  if (depthRb_) gl.DeleteRenderbuffers(1, &depthRb_);
  if (colorTex_) gl.DeleteTextures(1, &colorTex_);
  fbo_ = depthRb_ = colorTex_ = 0;
}

bool GlRenderer::init(u32 width, u32 height) {
  w_ = width;
  h_ = height;
  if (!loadGl()) return false;
  if (!batch_.init(kSpriteCapacity)) {
    RLOG_ERROR("gl", "sprite batch allocation failed");
    return false;
  }
  if (!compile(progMesh_, kMeshVs, kMeshFs) || !compile(progSprite_, kSpriteVs, kSpriteFs) ||
      !compile(progPresent_, kPresentVs, kPresentFs))
    return false;
  uMvp_ = gl.GetUniformLocation(progMesh_, "uMVP");
  uInvVirt_ = gl.GetUniformLocation(progSprite_, "uInvVirt");
  gl.UseProgram(progSprite_);
  gl.Uniform1i(gl.GetUniformLocation(progSprite_, "uTex"), 0);
  gl.UseProgram(progPresent_);
  gl.Uniform1i(gl.GetUniformLocation(progPresent_, "uTex"), 0);

  // Mesh VAO: PackVertex = color (4 x u8) then position (3 x float), 16 bytes.
  gl.GenVertexArrays(1, &vaoMesh_);
  gl.GenBuffers(1, &vboMesh_);
  gl.BindVertexArray(vaoMesh_);
  gl.BindBuffer(GL_ARRAY_BUFFER, vboMesh_);
  gl.EnableVertexAttribArray(0);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(PackVertex), reinterpret_cast<const void*>(4));
  gl.EnableVertexAttribArray(1);
  gl.VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, 1, sizeof(PackVertex), reinterpret_cast<const void*>(0));

  // Sprite VAO: one VBO sized for the whole batch, refilled every frame.
  gl.GenVertexArrays(1, &vaoSprite_);
  gl.GenBuffers(1, &vboSprite_);
  gl.BindVertexArray(vaoSprite_);
  gl.BindBuffer(GL_ARRAY_BUFFER, vboSprite_);
  gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(SpriteVertex)) * kSpriteCapacity * 6, nullptr,
                GL_STREAM_DRAW);
  gl.EnableVertexAttribArray(0);
  gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(SpriteVertex), reinterpret_cast<const void*>(0));
  gl.EnableVertexAttribArray(1);
  gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(SpriteVertex), reinterpret_cast<const void*>(8));
  gl.EnableVertexAttribArray(2);
  gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, sizeof(SpriteVertex), reinterpret_cast<const void*>(16));
  gl.GenVertexArrays(1, &vaoEmpty_);
  gl.BindVertexArray(0);

  if (!createTarget()) return false;
  ready_ = true;
  const u32 white = 0xFFFFFFFFu;
  TextureDesc wd;
  wd.width = wd.height = 1;
  wd.format = tex_format::RGBA8;
  wd.pixels = &white;
  whiteTex_ = uploadTexture(wd);
  RLOG_INFO("gl", "renderer ready virtual=%ux%u (%s | %s)", width, height,
            reinterpret_cast<const char*>(gl.GetString(GL_VERSION)), reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)));
  return whiteTex_ != 0;
}

void GlRenderer::setVirtualResolution(u32 w, u32 h) {
  if (!w || !h || (w == w_ && h == h_)) return;
  w_ = w;
  h_ = h;
  if (ready_) {
    destroyTarget();
    createTarget();
  }
}

void GlRenderer::setMeshes(const MeshTable& table) {
  meshes_ = table;
  if (!ready_ || !table.vertices || !table.vertexCount) return;
  gl.BindVertexArray(vaoMesh_);
  gl.BindBuffer(GL_ARRAY_BUFFER, vboMesh_);
  gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(table.vertexCount) * sizeof(PackVertex), table.vertices,
                GL_STATIC_DRAW);
  gl.BindVertexArray(0);
}

void GlRenderer::beginFrame(Color32 c) {
  batch_.clear();
  cameraSet_ = false;
  lastDraws_ = 0;
  gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  gl.Viewport(0, 0, static_cast<GLsizei>(w_), static_cast<GLsizei>(h_));
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_CULL_FACE);
  gl.Enable(GL_DEPTH_TEST);
  gl.DepthFunc(GL_LEQUAL);
  depthOn_ = true;
  gl.Disable(GL_BLEND);
  gl.ClearColor(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GlRenderer::setCamera(const Matrix4& view, const Matrix4& proj) {
  view_ = view;
  proj_ = proj;
  cameraSet_ = true;
}

void GlRenderer::drawMesh(u32 meshIndex, const Matrix4& world, u32) {
  // Depth buffer orders 3D; the sort key only matters to renderers without one.
  if (!cameraSet_ || meshIndex >= meshes_.meshCount || !meshes_.meshes) return;
  const PackMeshRec& m = meshes_.meshes[meshIndex];
  if (m.primitiveType != 0 || m.vertexOffset + m.vertexCount > meshes_.vertexCount) return;
  const Matrix4 mvp = proj_ * view_ * world;
  gl.UseProgram(progMesh_);
  gl.UniformMatrix4fv(uMvp_, 1, 0, mvp.m);
  gl.BindVertexArray(vaoMesh_);
  gl.DrawArrays(GL_TRIANGLES, static_cast<GLint>(m.vertexOffset), static_cast<GLsizei>(m.vertexCount));
  ++lastDraws_;
}

void GlRenderer::drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) {
  drawRect(Rect(static_cast<Scalar>(x), static_cast<Scalar>(y), static_cast<Scalar>(w), static_cast<Scalar>(h)), color,
           0xFFFFFF00u);  // overlays sit on top of sprites
}

void GlRenderer::releaseUserTextures() {
  if (!ready_) return;
  for (u32 i = whiteTex_; i < texCount_; ++i) gl.DeleteTextures(1, &tex_[i].id);  // handle = index + 1
  if (texCount_ > whiteTex_) texCount_ = whiteTex_;
}

u32 GlRenderer::uploadTexture(const TextureDesc& d) {
  if (!ready_ && !gl.GenTextures) return 0;
  if (!d.width || !d.height || !d.pixels || texCount_ >= kMaxTextures) return 0;
  if (d.flags != 0) {  // swizzled (PSP) data: only the GU renderer reads it
    RLOG_ERROR("gl", "texture upload: flags 0x%x (swizzled) are not supported", d.flags);
    return 0;
  }
  const u32 n = d.width * d.height;
  const void* rgba = d.pixels;
  u32* expanded = nullptr;
  if (d.format == tex_format::CLUT4 || d.format == tex_format::CLUT8) {
    // Palettes are expanded here, once, at upload (not per frame).
    expanded = static_cast<u32*>(malloc(static_cast<size_t>(n) * 4));
    const u32 pal = d.format == tex_format::CLUT4 ? 16 : 256;
    if (!expanded || !expandClut(d, pal, expanded)) {
      free(expanded);
      RLOG_ERROR("gl", "texture upload: bad CLUT texture");
      return 0;
    }
    rgba = expanded;
  } else if (d.format != tex_format::RGBA8) {
    RLOG_ERROR("gl", "texture upload: unknown format %u", static_cast<unsigned>(d.format));
    return 0;
  }
  Tex& t = tex_[texCount_];
  gl.GenTextures(1, &t.id);
  gl.BindTexture(GL_TEXTURE_2D, t.id);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(d.width), static_cast<GLsizei>(d.height), 0, GL_RGBA,
                GL_UNSIGNED_BYTE, rgba);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  free(expanded);
  t.w = d.width;
  t.h = d.height;
  return ++texCount_;  // handle = index + 1
}

void GlRenderer::drawSprite(u32 tex, const Rect& src, const Rect& dst, Color32 tint, u32 sortKey) {
  if (tex == 0 || tex > texCount_) return;
  const Tex& t = tex_[tex - 1];
  const float iw = 1.0f / static_cast<float>(t.w), ih = 1.0f / static_cast<float>(t.h);
  if (!batch_.add(tex, dst.x, dst.y, dst.x + dst.width, dst.y + dst.height, src.x * iw, src.y * ih,
                  (src.x + src.width) * iw, (src.y + src.height) * ih, tint, sortKey) &&
      !warnedFull_) {
    warnedFull_ = true;
    RLOG_WARN("gl", "sprite batch full (%u quads); further sprites are dropped", kSpriteCapacity);
  }
}

void GlRenderer::drawRect(const Rect& dst, Color32 color, u32 sortKey) {
  if (!whiteTex_) return;
  batch_.add(whiteTex_, dst.x, dst.y, dst.x + dst.width, dst.y + dst.height, 0, 0, 1, 1, color, sortKey);
}

void GlRenderer::drawText(u32 fontTex, const PackFontRec& f, i32 x, i32 y, const char* text, Color32 color,
                          u32 sortKey) {
  if (!text || fontTex == 0 || fontTex > texCount_ || !f.glyphW || !f.glyphH || !f.columns) return;
  const Tex& t = tex_[fontTex - 1];
  const float iw = 1.0f / static_cast<float>(t.w), ih = 1.0f / static_cast<float>(t.h);
  const u32 count = f.charCount < kPackFontMaxChars ? f.charCount : kPackFontMaxChars;
  i32 cx = x, cy = y;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p == '\n') {
      cx = x;
      cy += f.lineHeight ? f.lineHeight : f.glyphH;
      continue;
    }
    if (*p < f.firstChar || static_cast<u32>(*p - f.firstChar) >= count) continue;
    const u32 idx = static_cast<u32>(*p - f.firstChar);
    const float sx = static_cast<float>((idx % f.columns) * f.glyphW), sy = static_cast<float>((idx / f.columns) * f.glyphH);
    batch_.add(fontTex, static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cx + f.glyphW),
               static_cast<float>(cy + f.glyphH), sx * iw, sy * ih, (sx + f.glyphW) * iw, (sy + f.glyphH) * ih, color,
               sortKey);
    cx += f.advance[idx];
  }
}

void GlRenderer::flush2d() {
  lastSprites_ = batch_.count();
  const u32 verts = batch_.build();
  if (!verts) return;
  gl.Disable(GL_DEPTH_TEST);
  gl.Enable(GL_BLEND);
  gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl.UseProgram(progSprite_);
  gl.Uniform2f(uInvVirt_, 1.0f / static_cast<float>(w_), 1.0f / static_cast<float>(h_));
  gl.BindVertexArray(vaoSprite_);
  gl.BindBuffer(GL_ARRAY_BUFFER, vboSprite_);
  gl.BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(verts) * sizeof(SpriteVertex), batch_.vertices());
  gl.ActiveTexture(GL_TEXTURE0);
  for (u32 i = 0; i < batch_.runCount(); ++i) {
    const BatchRun& r = batch_.runs()[i];
    gl.BindTexture(GL_TEXTURE_2D, tex_[r.tex - 1].id);
    gl.DrawArrays(GL_TRIANGLES, static_cast<GLint>(r.first), static_cast<GLsizei>(r.count));
    ++lastDraws_;
  }
  gl.Disable(GL_BLEND);
}

void GlRenderer::endFrame() {
  flush2d();
  if (offscreen_) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return;
  }
  // Present: scale the virtual image into the window.
  SDL_Window* win = SDL_GL_GetCurrentWindow();
  if (!win) return;
  int dw = 0, dh = 0;
  SDL_GL_GetDrawableSize(win, &dw, &dh);
  winW_ = static_cast<u32>(dw);
  winH_ = static_cast<u32>(dh);
  const LetterboxRect lb = computeLetterbox(w_, h_, winW_, winH_);
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.Viewport(0, 0, dw, dh);
  gl.Disable(GL_DEPTH_TEST);
  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  gl.Viewport(lb.x, dh - lb.y - lb.h, lb.w, lb.h);  // GL viewport origin is bottom-left
  gl.UseProgram(progPresent_);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, colorTex_);
  const GLint filter = lb.integerScale ? GL_NEAREST : GL_LINEAR;
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  gl.BindVertexArray(vaoEmpty_);
  gl.DrawArrays(GL_TRIANGLES, 0, 3);
  gl.BindVertexArray(0);
  SDL_GL_SwapWindow(win);
}

bool GlRenderer::screenshot(const char* pngPath) {
  if (!ready_ || !pngPath) return false;
  const size_t bytes = static_cast<size_t>(w_) * h_ * 4;
  u8* px = static_cast<u8*>(malloc(bytes));
  if (!px) return false;
  gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.ReadPixels(0, 0, static_cast<GLsizei>(w_), static_cast<GLsizei>(h_), GL_RGBA, GL_UNSIGNED_BYTE, px);
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  for (size_t i = 3; i < bytes; i += 4) px[i] = 255;  // the clear alpha is not part of the picture
  // GL rows run bottom to top: a negative stride writes them upright.
  const int ok = stbi_write_png(pngPath, static_cast<int>(w_), static_cast<int>(h_), 4,
                                px + static_cast<size_t>(h_ - 1) * w_ * 4, -static_cast<int>(w_) * 4);
  free(px);
  if (!ok) RLOG_ERROR("gl", "cannot write screenshot %s", pngPath);
  return ok != 0;
}

}  // namespace rensga
