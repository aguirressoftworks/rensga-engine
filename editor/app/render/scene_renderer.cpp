#define GL_GLEXT_PROTOTYPES 1
#include "render/scene_renderer.h"

#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#include <cmath>
#include <cstdio>

namespace rensga {
namespace editor {

static unsigned compile(unsigned type, const char* src) {
  unsigned s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  int ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log); std::fprintf(stderr, "shader: %s\n", log); }
  return s;
}
static unsigned link(const char* vs, const char* fs) {
  unsigned p = glCreateProgram(), v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  glAttachShader(p, v); glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v); glDeleteShader(f);
  int ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { std::fprintf(stderr, "program link failed\n"); glDeleteProgram(p); return 0; }
  return p;
}

static const char* kMeshVs = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
uniform mat4 uMVP; uniform mat4 uModel; uniform vec2 uShift;
out vec3 vNrm;
void main(){
  vNrm = transpose(inverse(mat3(uModel))) * aNrm;
  gl_Position = uMVP * vec4(aPos,1.0);
  gl_Position.xy += uShift * gl_Position.w;
})";
static const char* kMeshFs = R"(#version 330 core
in vec3 vNrm; out vec4 o;
uniform vec4 uColor; uniform vec3 uLightDir; uniform vec3 uLightColor; uniform float uAmbient; uniform int uUnlit;
void main(){
  if (uUnlit != 0) { o = uColor; return; }
  float l = max(dot(normalize(vNrm), -normalize(uLightDir)), 0.0);
  float k = uAmbient + (1.0 - uAmbient) * l;
  o = vec4(uColor.rgb * uLightColor * k, 1.0);
})";
static const char* kGridVs = R"(#version 330 core
layout(location=0) in vec2 aXZ;
uniform mat4 uVP; uniform vec3 uEye; uniform float uSize;
out vec2 vXZ;
void main(){
  vec2 p = aXZ * uSize + uEye.xz;
  vXZ = p;
  gl_Position = uVP * vec4(p.x, 0.0, p.y, 1.0);
})";
static const char* kGridFs = R"(#version 330 core
in vec2 vXZ; out vec4 o;
uniform vec3 uEye; uniform float uFar; uniform vec4 uGrid; uniform vec4 uXc; uniform vec4 uZc;
float lines(float s){
  vec2 c = vXZ / s; vec2 w = fwidth(c);
  vec2 a = abs(fract(c - 0.5) - 0.5) / max(w, vec2(1e-4));
  return 1.0 - min(min(a.x, a.y), 1.0);
}
void main(){
  float d = length(vXZ - uEye.xz);
  float fade = 1.0 - smoothstep(uFar * 0.03, uFar * 0.2, d);
  float minor = lines(1.0) * 0.45 * (1.0 - smoothstep(8.0, 35.0, d));
  float major = lines(10.0) * 0.9;
  float a = max(minor, major) * uGrid.a;
  vec3 col = uGrid.rgb;
  vec2 w = fwidth(vXZ);
  float ax = 1.0 - min(abs(vXZ.y) / max(w.y, 1e-4), 1.0);   // X axis lies on z = 0
  float az = 1.0 - min(abs(vXZ.x) / max(w.x, 1e-4), 1.0);   // Z axis lies on x = 0
  if (ax > 0.0) { col = mix(col, uXc.rgb, ax); a = max(a, ax * 0.9); }
  if (az > 0.0) { col = mix(col, uZc.rgb, az); a = max(a, az * 0.9); }
  o = vec4(col, a * fade);
  if (o.a < 0.003) discard;
})";
static const char* kLineVs = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol;
uniform mat4 uVP; out vec4 vCol;
void main(){ vCol = aCol; gl_Position = uVP * vec4(aPos,1.0); })";
static const char* kLineFs = R"(#version 330 core
in vec4 vCol; out vec4 o; void main(){ o = vCol; })";

bool SceneRenderer::init() {
  if (prog_) return true;
  prog_ = link(kMeshVs, kMeshFs);
  gridProg_ = link(kGridVs, kGridFs);
  lineProg_ = link(kLineVs, kLineFs);
  const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
  glGenVertexArrays(1, &gridVao_); glGenBuffers(1, &gridVbo_);
  glBindVertexArray(gridVao_);
  glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
  glGenVertexArrays(1, &lineVao_); glGenBuffers(1, &lineVbo_);
  glBindVertexArray(lineVao_);
  glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, nullptr);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, reinterpret_cast<void*>(12));
  glBindVertexArray(0);
  return prog_ && gridProg_ && lineProg_;
}

const SceneRenderer::Gpu& SceneRenderer::upload(const std::shared_ptr<const MeshData>& m) {
  auto it = gpu_.find(m.get());
  if (it != gpu_.end()) return it->second;
  Gpu g;
  g.mesh = m;
  g.count = static_cast<int>(m->pos.size() / 3);
  std::vector<float> inter(m->pos.size() * 2);
  for (int i = 0; i < g.count; ++i)
    for (int k = 0; k < 3; ++k) { inter[i * 6 + k] = m->pos[i * 3 + k]; inter[i * 6 + 3 + k] = m->nrm[i * 3 + k]; }
  glGenVertexArrays(1, &g.vao); glGenBuffers(1, &g.vbo);
  glBindVertexArray(g.vao);
  glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
  glBufferData(GL_ARRAY_BUFFER, inter.size() * sizeof(float), inter.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
  glBindVertexArray(0);
  return gpu_.emplace(m.get(), g).first->second;
}

void SceneRenderer::drawLines(const std::vector<float>& v, const M4& vp) {
  if (v.empty()) return;
  glUseProgram(lineProg_);
  glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uVP"), 1, GL_FALSE, vp.data());
  glBindVertexArray(lineVao_);
  glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_LINES, 0, static_cast<int>(v.size() / 7));
  glBindVertexArray(0);
}

void RenderTarget::destroy() {
  if (fbo) glDeleteFramebuffers(1, &fbo);
  if (msFbo) glDeleteFramebuffers(1, &msFbo);
  if (tex) glDeleteTextures(1, &tex);
  if (depthTex) glDeleteRenderbuffers(1, &depthTex);
  if (rbColor) glDeleteRenderbuffers(1, &rbColor);
  if (rbDepth) glDeleteRenderbuffers(1, &rbDepth);
  fbo = msFbo = tex = rbColor = rbDepth = depthTex = 0;
  w = h = 0;
}

void RenderTarget::ensure(int width, int height, int msaa, bool nearest) {
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  if (fbo && w == width && h == height) return;
  destroy();
  w = width; h = height; samples = msaa;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  GLint f = nearest ? GL_NEAREST : GL_LINEAR;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glGenRenderbuffers(1, &rbDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, rbDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbDepth);
  if (samples > 1) {
    GLuint ms = 0, c = 0, d = 0;
    glGenFramebuffers(1, &ms); glGenRenderbuffers(1, &c); glGenRenderbuffers(1, &d);
    glBindRenderbuffer(GL_RENDERBUFFER, c);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, d);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, ms);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, d);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) { msFbo = ms; rbColor = c; depthTex = d; }
    else { glDeleteFramebuffers(1, &ms); glDeleteRenderbuffers(1, &c); glDeleteRenderbuffers(1, &d); samples = 0; }
  }
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void pushLine(std::vector<float>& v, const float a[3], const float b[3], const float c[4]) {
  v.insert(v.end(), a, a + 3); v.insert(v.end(), c, c + 4);
  v.insert(v.end(), b, b + 3); v.insert(v.end(), c, c + 4);
}

static void markerLines(std::vector<float>& v, const Marker& m, const float col[4]) {
  auto P = [&](float x, float y, float z, float out[3]) { const float p[3] = {x, y, z}; m4Point(m.model, p, out); };
  float a[3], b[3];
  if (m.kind == "Camera") {
    const float hw = 0.6f, hh = 0.34f, z = 0.8f;
    float apex[3], c[4][3];
    P(0, 0, 0, apex);
    P(-hw, -hh, z, c[0]); P(hw, -hh, z, c[1]); P(hw, hh, z, c[2]); P(-hw, hh, z, c[3]);
    for (int i = 0; i < 4; ++i) { pushLine(v, apex, c[i], col); pushLine(v, c[i], c[(i + 1) % 4], col); }
  } else if (m.kind == "DirectionalLight") {
    P(0, 0, 0, a); P(0, 0, 1.6f, b); pushLine(v, a, b, col);
    P(0.15f, 0, 1.3f, a); pushLine(v, a, b, col);
    P(-0.15f, 0, 1.3f, a); pushLine(v, a, b, col);
    for (int i = 0; i < 8; ++i) {  // sun ring
      float t0 = i * 0.785398f, t1 = (i + 1) * 0.785398f;
      float p0[3], p1[3];
      P(std::cos(t0) * 0.25f, std::sin(t0) * 0.25f, 0, p0); P(std::cos(t1) * 0.25f, std::sin(t1) * 0.25f, 0, p1);
      pushLine(v, p0, p1, col);
    }
  } else {
    const float r = 0.25f;
    P(-r, 0, 0, a); P(r, 0, 0, b); pushLine(v, a, b, col);
    P(0, -r, 0, a); P(0, r, 0, b); pushLine(v, a, b, col);
    P(0, 0, -r, a); P(0, 0, r, b); pushLine(v, a, b, col);
  }
}

void SceneRenderer::render(RenderTarget& rt, const SceneDraw& sd, const RenderOptions& opt) {
  if (!ready() || !rt.fbo) return;
  GLint prevFbo = 0, prevVp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_VIEWPORT, prevVp);
  glBindFramebuffer(GL_FRAMEBUFFER, rt.msFbo ? rt.msFbo : rt.fbo);
  glViewport(0, 0, rt.w, rt.h);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(opt.clear[0], opt.clear[1], opt.clear[2], opt.clear[3]);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  M4 vp = m4Mul(opt.proj, opt.view);
  glUseProgram(prog_);
  glUniform3fv(glGetUniformLocation(prog_, "uLightDir"), 1, sd.light.dir);
  glUniform3fv(glGetUniformLocation(prog_, "uLightColor"), 1, sd.light.color);
  glUniform1f(glGetUniformLocation(prog_, "uAmbient"), sd.light.ambient);
  const int uMVP = glGetUniformLocation(prog_, "uMVP"), uModel = glGetUniformLocation(prog_, "uModel"),
            uColor = glGetUniformLocation(prog_, "uColor"), uUnlit = glGetUniformLocation(prog_, "uUnlit"),
            uShift = glGetUniformLocation(prog_, "uShift");
  glUniform1i(uUnlit, 0);
  glUniform2f(uShift, 0, 0);
  for (const Drawable& d : sd.drawables) {
    const Gpu& g = upload(d.mesh);
    M4 mvp = m4Mul(vp, d.model);
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.data());
    glUniformMatrix4fv(uModel, 1, GL_FALSE, d.model.data());
    glUniform4fv(uColor, 1, d.color);
    glBindVertexArray(g.vao);
    glDrawArrays(GL_TRIANGLES, 0, g.count);
  }

  // selection outline: wireframe of the selected mesh, a few pixels thick (offset copies), in the primary token
  if (!opt.selected.empty()) {
    glUniform1i(uUnlit, 1);
    glUniform4fv(uColor, 1, opt.selectColor);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.5f, -1.5f);
    const float px = 1.0f / static_cast<float>(rt.w), py = 1.0f / static_cast<float>(rt.h);
    const float off[5][2] = {{0, 0}, {px, 0}, {-px, 0}, {0, py}, {0, -py}};
    for (const Drawable& d : sd.drawables) {
      if (d.guid != opt.selected) continue;
      const Gpu& g = upload(d.mesh);
      M4 mvp = m4Mul(vp, d.model);
      glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.data());
      glUniformMatrix4fv(uModel, 1, GL_FALSE, d.model.data());
      glBindVertexArray(g.vao);
      for (auto& o : off) { glUniform2f(uShift, o[0], o[1]); glDrawArrays(GL_TRIANGLES, 0, g.count); }
    }
    glUniform2f(uShift, 0, 0);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  glBindVertexArray(0);
  if (opt.grid) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    glUseProgram(gridProg_);
    glUniformMatrix4fv(glGetUniformLocation(gridProg_, "uVP"), 1, GL_FALSE, vp.data());
    glUniform3fv(glGetUniformLocation(gridProg_, "uEye"), 1, opt.eye);
    glUniform1f(glGetUniformLocation(gridProg_, "uSize"), opt.farClip * 0.9f);
    glUniform1f(glGetUniformLocation(gridProg_, "uFar"), opt.farClip);
    glUniform4fv(glGetUniformLocation(gridProg_, "uGrid"), 1, opt.gridColor);
    glUniform4fv(glGetUniformLocation(gridProg_, "uXc"), 1, opt.xColor);
    glUniform4fv(glGetUniformLocation(gridProg_, "uZc"), 1, opt.zColor);
    glBindVertexArray(gridVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }
  if (opt.markers) {
    std::vector<float> lines;
    for (const Marker& m : sd.markers) markerLines(lines, m, m.guid == opt.selected ? opt.selectColor : opt.markerColor);
    drawLines(lines, vp);
  }

  if (rt.msFbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, rt.msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rt.fbo);
    glBlitFramebuffer(0, 0, rt.w, rt.h, 0, 0, rt.w, rt.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
  glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
  glUseProgram(0);
  glDisable(GL_DEPTH_TEST);
}

}  // namespace editor
}  // namespace rensga
