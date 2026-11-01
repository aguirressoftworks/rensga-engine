#define GL_GLEXT_PROTOTYPES 1
#include "render/scene_renderer.h"

#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "rensga/core/gizmos.h"

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
uniform vec3 uEye; uniform float uFar; uniform vec4 uGrid; uniform vec4 uXc; uniform vec4 uZc; uniform float uStep;
float lines(float s){
  vec2 c = vXZ / s; vec2 w = fwidth(c);
  vec2 a = abs(fract(c - 0.5) - 0.5) / max(w, vec2(1e-4));
  return 1.0 - min(min(a.x, a.y), 1.0);
}
void main(){
  float d = length(vXZ - uEye.xz);
  float fade = 1.0 - smoothstep(uFar * 0.03, uFar * 0.2, d);
  float minor = lines(uStep) * 0.45 * (1.0 - smoothstep(8.0 * uStep, 35.0 * uStep, d));
  float major = lines(uStep * 10.0) * 0.9;
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

static const char* kSpriteVs = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; layout(location=2) in vec4 aCol; layout(location=3) in uint aId;
uniform mat4 uVP; out vec2 vUv; out vec4 vCol; flat out uint vId;
void main(){ vUv = aUv; vCol = aCol; vId = aId; gl_Position = uVP * vec4(aPos, 1.0); })";
static const char* kSpriteFs = R"(#version 330 core
in vec2 vUv; in vec4 vCol; flat in uint vId; out vec4 o; uniform sampler2D uTex;
void main(){ o = texture(uTex, vUv) * vCol; if (o.a < 0.004) discard; })";
static const char* kPickSpriteFs = R"(#version 330 core
in vec2 vUv; in vec4 vCol; flat in uint vId; layout(location=0) out uint o; uniform sampler2D uTex;
void main(){ vec4 c = texture(uTex, vUv) * vCol; if (c.a < 0.1) discard; o = vId; })";
static const char* kPickMeshFs = R"(#version 330 core
in vec3 vNrm; layout(location=0) out uint o; uniform uint uId;
void main(){ o = uId; })";

static const char* kBlitVs = R"(#version 330 core
out vec2 vUv;
void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); vUv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";
static const char* kBlitFs = R"(#version 330 core
in vec2 vUv; out vec4 o; uniform sampler2D uTex;
void main(){ o = vec4(texture(uTex, vUv).rgb, 1.0); })";

bool SceneRenderer::init() {
  if (prog_) return true;
  blitProg_ = link(kBlitVs, kBlitFs);
  glGenVertexArrays(1, &blitVao_);
  prog_ = link(kMeshVs, kMeshFs);
  gridProg_ = link(kGridVs, kGridFs);
  lineProg_ = link(kLineVs, kLineFs);
  spriteProg_ = link(kSpriteVs, kSpriteFs);
  pickSpriteProg_ = link(kSpriteVs, kPickSpriteFs);
  pickMeshProg_ = link(kMeshVs, kPickMeshFs);
  {
    const unsigned char white[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenVertexArrays(1, &spriteVao_); glGenBuffers(1, &spriteVbo_);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(20));
    glEnableVertexAttribArray(3); glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, 40, reinterpret_cast<void*>(36));
    glBindVertexArray(0);
    // unit cube (pick volume of markers: lights, cameras, empties)
    markerCube_ = std::make_shared<MeshData>();
    static const float f[6][4][3] = {{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}, {{1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}},
                                     {{-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1}}, {{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}},
                                     {{1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1}}, {{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (const auto& face : f)
      for (int k : idx) {
        for (int c = 0; c < 3; ++c) { markerCube_->pos.push_back(face[k][c] * 0.5f); markerCube_->nrm.push_back(c == 1 ? 1.0f : 0.0f); }
      }
    for (int i = 0; i < 3; ++i) { markerCube_->bmin[i] = -0.5f; markerCube_->bmax[i] = 0.5f; }
  }
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

void SceneRenderer::drawLines(const std::vector<float>& v, const M4& vp, unsigned mode) {
  if (v.empty()) return;
  glUseProgram(lineProg_);
  glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uVP"), 1, GL_FALSE, vp.data());
  glBindVertexArray(lineVao_);
  glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
  glDrawArrays(mode, 0, static_cast<int>(v.size() / 7));
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
    const float hw = 0.6f, hh = 0.34f, z = -0.8f;  // the engine camera looks down its local -z
    float apex[3], c[4][3];
    P(0, 0, 0, apex);
    P(-hw, -hh, z, c[0]); P(hw, -hh, z, c[1]); P(hw, hh, z, c[2]); P(-hw, hh, z, c[3]);
    for (int i = 0; i < 4; ++i) { pushLine(v, apex, c[i], col); pushLine(v, c[i], c[(i + 1) % 4], col); }
  } else if (m.kind == "PointLight") {  // F6.02: three rings around the lamp
    for (int ax = 0; ax < 3; ++ax)
      for (int i = 0; i < 12; ++i) {
        const float t0 = static_cast<float>(i) * 0.5235988f, t1 = static_cast<float>(i + 1) * 0.5235988f, r = 0.28f;
        float p0[3], p1[3];
        const float c0 = std::cos(t0) * r, s0 = std::sin(t0) * r, c1 = std::cos(t1) * r, s1 = std::sin(t1) * r;
        if (ax == 0) { P(c0, s0, 0, p0); P(c1, s1, 0, p1); }
        else if (ax == 1) { P(c0, 0, s0, p0); P(c1, 0, s1, p1); }
        else { P(0, c0, s0, p0); P(0, c1, s1, p1); }
        pushLine(v, p0, p1, col);
      }
    P(0, 0, 0, a); P(0, 0.12f, 0, b); pushLine(v, a, b, col);
  } else if (m.kind == "AudioSource") {  // a small speaker: box and cone
    float c[8][3];
    for (int k = 0; k < 8; ++k) P((k & 1) ? 0.12f : -0.12f, (k & 2) ? 0.12f : -0.12f, (k & 4) ? 0.0f : -0.12f, c[k]);
    for (int k = 0; k < 8; ++k)
      for (int bit = 1; bit < 8; bit <<= 1)
        if (!(k & bit)) pushLine(v, c[k], c[k | bit], col);
    float cone[4][3], tip[4][3];
    const float cs[4][2] = {{-0.12f, -0.12f}, {0.12f, -0.12f}, {0.12f, 0.12f}, {-0.12f, 0.12f}};
    for (int k = 0; k < 4; ++k) { P(cs[k][0], cs[k][1], 0.0f, cone[k]); P(cs[k][0] * 2.2f, cs[k][1] * 2.2f, 0.28f, tip[k]); pushLine(v, cone[k], tip[k], col); }
    for (int k = 0; k < 4; ++k) pushLine(v, tip[k], tip[(k + 1) % 4], col);
  } else if (m.kind == "DirectionalLight" || m.kind == "Light") {
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

void SceneRenderer::draw2dGrid(const RenderOptions& opt, const M4& vp) {
  // visible world rectangle: unproject the NDC corners onto z = 0
  const M4 inv = m4Invert(vp);
  float mn[2] = {1e30f, 1e30f}, mx[2] = {-1e30f, -1e30f};
  for (int i = 0; i < 4; ++i) {
    const float v[4] = {(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, 0, 1};
    float r[4];
    for (int k = 0; k < 4; ++k) r[k] = inv[k] * v[0] + inv[4 + k] * v[1] + inv[8 + k] * v[2] + inv[12 + k] * v[3];
    for (int k = 0; k < 2; ++k) { mn[k] = std::min(mn[k], r[k] / r[3]); mx[k] = std::max(mx[k], r[k] / r[3]); }
  }
  const float step = opt.gridStep2d > 0 ? opt.gridStep2d : 16.0f;
  if ((mx[0] - mn[0]) / step > 600 || (mx[1] - mn[1]) / step > 600) return;
  std::vector<float> v;
  auto line = [&](float x0, float y0, float x1, float y1, const float* c, float a) {
    const float p0[3] = {x0, y0, 0}, p1[3] = {x1, y1, 0}, col[4] = {c[0], c[1], c[2], c[3] * a};
    pushLine(v, p0, p1, col);
  };
  const long ix0 = static_cast<long>(std::floor(mn[0] / step)), ix1 = static_cast<long>(std::ceil(mx[0] / step));
  const long iy0 = static_cast<long>(std::floor(mn[1] / step)), iy1 = static_cast<long>(std::ceil(mx[1] / step));
  for (long i = ix0; i <= ix1; ++i) line(i * step, mn[1], i * step, mx[1], opt.gridColor, i % 10 == 0 ? 0.9f : 0.4f);
  for (long j = iy0; j <= iy1; ++j) line(mn[0], j * step, mx[0], j * step, opt.gridColor, j % 10 == 0 ? 0.9f : 0.4f);
  line(mn[0], 0, mx[0], 0, opt.xColor, 1);   // X axis
  line(0, mn[1], 0, mx[1], opt.zColor, 1);   // Y axis
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  drawLines(v, vp);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawQuads(const std::vector<SpriteQuad>& quads, const M4& vp, bool pickMode) {
  const unsigned prog = pickMode ? pickSpriteProg_ : spriteProg_;
  glUseProgram(prog);
  glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, vp.data());
  glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  if (!pickMode) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
  glBindVertexArray(spriteVao_);
  glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
  std::vector<float> v;
  v.reserve(4096 * 60);
  auto flush = [&](unsigned tex) {
    if (v.empty()) return;
    glBindTexture(GL_TEXTURE_2D, tex ? tex : whiteTex_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<long>(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(v.size() / 10));
    v.clear();
  };
  unsigned cur = ~0u;
  for (const SpriteQuad& q : quads) {
    if (pickMode && q.id == 0) continue;
    if (q.tex != cur) { flush(cur); cur = q.tex; }
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k : order) {
      v.insert(v.end(), q.p[k], q.p[k] + 3);
      v.insert(v.end(), q.uv[k], q.uv[k] + 2);
      v.insert(v.end(), q.color, q.color + 4);
      float idf;
      static_assert(sizeof(idf) == sizeof(q.id), "id is stored in a float slot");
      std::memcpy(&idf, &q.id, sizeof idf);
      v.push_back(idf);
    }
    if (v.size() > 4000 * 60) flush(cur);
  }
  flush(cur);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

void SceneRenderer::drawGizmoList(const M4& vp, bool mode2d) {
  const unsigned nLines = GizmoDraw::lineCount(), nTris = GizmoDraw::triangleCount();
  if (!nLines && !nTris) return;
  auto unpack = [](unsigned rgba, float out[4], float alphaScale) {
    out[0] = static_cast<float>(rgba & 255) / 255.0f;
    out[1] = static_cast<float>((rgba >> 8) & 255) / 255.0f;
    out[2] = static_cast<float>((rgba >> 16) & 255) / 255.0f;
    out[3] = static_cast<float>((rgba >> 24) & 255) / 255.0f * alphaScale;
  };
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  if (nTris) {
    std::vector<float> v;
    const GizmoTriangle* t = GizmoDraw::triangles();
    for (unsigned i = 0; i < nTris; ++i) {
      float c[4];
      unpack(t[i].rgba, c, 1);
      for (int k = 0; k < 3; ++k) { v.insert(v.end(), t[i].p + k * 3, t[i].p + k * 3 + 3); v.insert(v.end(), c, c + 4); }
    }
    mode2d ? glDisable(GL_DEPTH_TEST) : glEnable(GL_DEPTH_TEST);
    drawLines(v, vp, GL_TRIANGLES);
  }
  if (nLines) {
    const GizmoLine* l = GizmoDraw::lines();
    for (int pass = mode2d ? 1 : 0; pass < 2; ++pass) {  // pass 0: faint, through geometry; pass 1: full, depth tested
      std::vector<float> v;
      v.reserve(nLines * 14);
      for (unsigned i = 0; i < nLines; ++i) {
        float c[4];
        unpack(l[i].rgba, c, pass == 0 ? 0.3f : 1.0f);
        v.insert(v.end(), l[i].a, l[i].a + 3); v.insert(v.end(), c, c + 4);
        v.insert(v.end(), l[i].b, l[i].b + 3); v.insert(v.end(), c, c + 4);
      }
      (pass == 0 || mode2d) ? glDisable(GL_DEPTH_TEST) : glEnable(GL_DEPTH_TEST);
      drawLines(v, vp);
    }
  }
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void SceneRenderer::ensurePickTarget(int w, int h) {
  if (pickFbo_ && pickW_ == static_cast<unsigned>(w) && pickH_ == static_cast<unsigned>(h)) return;
  if (pickFbo_) { glDeleteFramebuffers(1, &pickFbo_); glDeleteRenderbuffers(1, &pickColor_); glDeleteRenderbuffers(1, &pickDepth_); }
  pickW_ = static_cast<unsigned>(w); pickH_ = static_cast<unsigned>(h);
  glGenFramebuffers(1, &pickFbo_); glGenRenderbuffers(1, &pickColor_); glGenRenderbuffers(1, &pickDepth_);
  glBindRenderbuffer(GL_RENDERBUFFER, pickColor_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_R32UI, w, h);
  glBindRenderbuffer(GL_RENDERBUFFER, pickDepth_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, pickColor_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickDepth_);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

std::vector<unsigned> SceneRenderer::pick(const SceneDraw& sd, const std::vector<SpriteQuad>* quads,
                                          const std::map<std::string, unsigned>& ids, const M4& view, const M4& proj, bool mode2d,
                                          int w, int h, int x0, int y0, int x1, int y1) {
  std::vector<unsigned> result;
  if (!ready() || w < 1 || h < 1) return result;
  x0 = std::max(0, std::min(x0, w)); x1 = std::max(0, std::min(x1, w));
  y0 = std::max(0, std::min(y0, h)); y1 = std::max(0, std::min(y1, h));
  if (x1 <= x0 || y1 <= y0) return result;
  GLint prevFbo = 0, prevVp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_VIEWPORT, prevVp);
  ensurePickTarget(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_);
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
  const GLuint zero[4] = {0, 0, 0, 0};
  glClearBufferuiv(GL_COLOR, 0, zero);
  glClearDepth(1.0);
  glClear(GL_DEPTH_BUFFER_BIT);
  const M4 vp = m4Mul(proj, view);
  glUseProgram(pickMeshProg_);
  const int uMVP = glGetUniformLocation(pickMeshProg_, "uMVP"), uModel = glGetUniformLocation(pickMeshProg_, "uModel"),
            uId = glGetUniformLocation(pickMeshProg_, "uId");
  auto idOf = [&](const std::string& g) { auto it = ids.find(g); return it == ids.end() ? 0u : it->second; };
  for (const Drawable& d : sd.drawables) {
    const unsigned id = idOf(d.guid);
    if (!id) continue;
    const Gpu& g = upload(d.mesh);
    const M4 mvp = m4Mul(vp, d.model);
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.data());
    glUniformMatrix4fv(uModel, 1, GL_FALSE, d.model.data());
    glUniform1ui(uId, id);
    glBindVertexArray(g.vao);
    glDrawArrays(GL_TRIANGLES, 0, g.count);
  }
  if (markerCube_) {
    const Gpu& g = upload(markerCube_);
    for (const Marker& m : sd.markers) {
      const unsigned id = idOf(m.guid);
      if (!id) continue;
      M4 model = m4Identity();
      model[0] = model[5] = model[10] = 0.8f;
      model[12] = m.model[12]; model[13] = m.model[13]; model[14] = m.model[14];
      const M4 mvp = m4Mul(vp, model);
      glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.data());
      glUniformMatrix4fv(uModel, 1, GL_FALSE, model.data());
      glUniform1ui(uId, id);
      glBindVertexArray(g.vao);
      glDrawArrays(GL_TRIANGLES, 0, g.count);
    }
  }
  glBindVertexArray(0);
  if (quads && !quads->empty()) drawQuads(*quads, vp, true);
  (void)mode2d;
  std::vector<GLuint> px(static_cast<size_t>(x1 - x0) * static_cast<size_t>(y1 - y0));
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(x0, h - y1, x1 - x0, y1 - y0, GL_RED_INTEGER, GL_UNSIGNED_INT, px.data());
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
  glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
  glUseProgram(0);
  glDisable(GL_DEPTH_TEST);
  // unique ids, nearest to the rectangle centre first
  const float cx = (x1 - x0 - 1) * 0.5f, cy = (y1 - y0 - 1) * 0.5f;
  std::map<unsigned, float> best;
  const int rw = x1 - x0, rh = y1 - y0;
  for (int j = 0; j < rh; ++j)
    for (int i = 0; i < rw; ++i) {
      const unsigned id = px[static_cast<size_t>(j) * static_cast<size_t>(rw) + static_cast<size_t>(i)];
      if (!id) continue;
      const float dx = static_cast<float>(i) - cx, dy = static_cast<float>(rh - 1 - j) - cy, d = dx * dx + dy * dy;
      auto it = best.find(id);
      if (it == best.end() || d < it->second) best[id] = d;
    }
  std::vector<std::pair<float, unsigned>> order;
  for (auto& kv : best) order.push_back({kv.second, kv.first});
  std::sort(order.begin(), order.end());
  for (auto& o : order) result.push_back(o.second);
  return result;
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
  std::set<std::string> sel(opt.selection.begin(), opt.selection.end());
  if (!opt.selected.empty()) sel.insert(opt.selected);
  if (opt.mode2d) draw2dGrid(opt, vp);
  glUseProgram(prog_);
  glUniform3fv(glGetUniformLocation(prog_, "uLightDir"), 1, sd.light.dir);
  glUniform3fv(glGetUniformLocation(prog_, "uLightColor"), 1, sd.light.color);
  glUniform1f(glGetUniformLocation(prog_, "uAmbient"), sd.light.ambient);
  const int uMVP = glGetUniformLocation(prog_, "uMVP"), uModel = glGetUniformLocation(prog_, "uModel"),
            uColor = glGetUniformLocation(prog_, "uColor"), uUnlit = glGetUniformLocation(prog_, "uUnlit"),
            uShift = glGetUniformLocation(prog_, "uShift");
  glUniform1i(uUnlit, 0);
  glUniform2f(uShift, 0, 0);
  if (opt.litTex && blitProg_) {  // F6.02: the lit image first, then the meshes again for depth only
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(blitProg_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, opt.litTex);
    glUniform1i(glGetUniformLocation(blitProg_, "uTex"), 0);
    glBindVertexArray(blitVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glUseProgram(prog_);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  }
  for (const Drawable& d : sd.drawables) {
    const Gpu& g = upload(d.mesh);
    M4 mvp = m4Mul(vp, d.model);
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.data());
    glUniformMatrix4fv(uModel, 1, GL_FALSE, d.model.data());
    glUniform4fv(uColor, 1, d.color);
    glBindVertexArray(g.vao);
    glDrawArrays(GL_TRIANGLES, 0, g.count);
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  // selection outline: wireframe of the selected mesh, a few pixels thick (offset copies), in the primary token
  if (!sel.empty()) {
    glUniform1i(uUnlit, 1);
    glUniform4fv(uColor, 1, opt.selectColor);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.5f, -1.5f);
    const float px = 1.0f / static_cast<float>(rt.w), py = 1.0f / static_cast<float>(rt.h);
    const float off[5][2] = {{0, 0}, {px, 0}, {-px, 0}, {0, py}, {0, -py}};
    for (const Drawable& d : sd.drawables) {
      if (!sel.count(d.guid)) continue;
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
  if (opt.grid && !opt.mode2d) {
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
    glUniform1f(glGetUniformLocation(gridProg_, "uStep"), opt.gridStep > 0 ? opt.gridStep : 1.0f);
    glBindVertexArray(gridVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }
  if (opt.markers) {
    std::vector<float> lines;
    for (const Marker& m : sd.markers) markerLines(lines, m, sel.count(m.guid) ? opt.selectColor : opt.markerColor);
    drawLines(lines, vp);
  }
  if (opt.quads && !opt.quads->empty()) drawQuads(*opt.quads, vp, false);
  if (opt.gizmos) drawGizmoList(vp, opt.mode2d);

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
