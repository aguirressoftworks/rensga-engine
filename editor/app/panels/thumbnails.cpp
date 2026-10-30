#include "panels/thumbnails.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cstdio>

#include "rensga/cooker/assets.h"

namespace rensga {
namespace editor {

void GlImage::upload(const uint8_t* rgba, int width, int height, bool nearest) {
  if (!tex) glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  const GLint f = nearest ? GL_NEAREST : GL_LINEAR;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  w = width;
  h = height;
}

void GlImage::destroy() {
  if (tex) { GLuint t = tex; glDeleteTextures(1, &t); }
  tex = 0;
  w = h = 0;
}

static bool readFile(const std::filesystem::path& p, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) return false;
  uint8_t buf[65536];
  size_t n;
  out.clear();
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return true;
}

Thumbnails::~Thumbnails() {
  {
    std::lock_guard<std::mutex> l(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  for (auto& kv : items_) kv.second.img.destroy();
}

void Thumbnails::startThread() {
  if (started_) return;
  started_ = true;
  thread_ = std::thread([this] { worker(); });
}

void Thumbnails::worker() {
  for (;;) {
    Req r;
    {
      std::unique_lock<std::mutex> l(mu_);
      cv_.wait(l, [&] { return stop_ || !queue_.empty(); });
      if (stop_) return;
      r = queue_.front();
      queue_.pop_front();
    }
    Done d;
    d.guid = r.guid;
    d.epoch = r.epoch;
    std::vector<uint8_t> bytes;
    ImageRgba img;
    std::string err;
    if (readFile(r.path, bytes) && decodePng(bytes, img, err) && img.width && img.height) {
      const uint32_t maxEdge = 96;
      const uint32_t big = std::max(img.width, img.height);
      const uint32_t step = (big + maxEdge - 1) / maxEdge;  // box filter by integer steps
      d.w = static_cast<int>(std::max(1u, img.width / std::max(1u, step)));
      d.h = static_cast<int>(std::max(1u, img.height / std::max(1u, step)));
      d.rgba.assign(static_cast<size_t>(d.w) * d.h * 4, 0);
      for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
          uint32_t sum[4] = {0, 0, 0, 0}, n = 0;
          for (uint32_t sy = 0; sy < step; ++sy)
            for (uint32_t sx = 0; sx < step; ++sx) {
              const uint32_t px = x * step + sx, py = y * step + sy;
              if (px >= img.width || py >= img.height) continue;
              const uint8_t* s = &img.rgba[(static_cast<size_t>(py) * img.width + px) * 4];
              const uint32_t a = s[3];  // premultiplied average so transparent pixels do not darken edges
              sum[0] += s[0] * a; sum[1] += s[1] * a; sum[2] += s[2] * a; sum[3] += a;
              ++n;
            }
          uint8_t* o = &d.rgba[(static_cast<size_t>(y) * d.w + x) * 4];
          if (sum[3]) { o[0] = static_cast<uint8_t>(sum[0] / sum[3]); o[1] = static_cast<uint8_t>(sum[1] / sum[3]); o[2] = static_cast<uint8_t>(sum[2] / sum[3]); }
          o[3] = n ? static_cast<uint8_t>(sum[3] / n) : 0;
        }
      d.ok = true;
    }
    std::lock_guard<std::mutex> l(mu_);
    done_.push_back(std::move(d));
  }
}

ImTextureID Thumbnails::get(const std::string& guid, const std::filesystem::path& png) {
  Item& it = items_[guid];
  it.used = ++tick_;
  if (it.img.tex) return it.img.id();
  if (!it.requested && !it.failed) {
    it.requested = true;
    startThread();
    {
      std::lock_guard<std::mutex> l(mu_);
      queue_.push_back({guid, png, epoch_[guid]});
    }
    cv_.notify_one();
  }
  return 0;
}

void Thumbnails::pump() {
  std::vector<Done> ready;
  {
    std::lock_guard<std::mutex> l(mu_);
    ready.swap(done_);
  }
  for (Done& d : ready) {
    const std::string& guid = d.guid;
    {
      std::lock_guard<std::mutex> l(mu_);
      if (epoch_[guid] != d.epoch) continue;  // invalidated while decoding; the next get() asks again
    }
    auto it = items_.find(guid);
    if (it == items_.end()) continue;
    if (d.ok) it->second.img.upload(d.rgba.data(), d.w, d.h, false);
    else it->second.failed = true;
  }
  if (items_.size() > cap) {  // free the least recently used textures
    std::vector<std::pair<uint64_t, std::string>> order;
    for (auto& kv : items_) order.emplace_back(kv.second.used, kv.first);
    std::sort(order.begin(), order.end());
    for (size_t i = 0; i < order.size() && items_.size() > cap; ++i) {
      auto it = items_.find(order[i].second);
      if (it->second.requested && !it->second.img.tex && !it->second.failed) continue;  // decoding
      it->second.img.destroy();
      items_.erase(it);
    }
  }
}

void Thumbnails::invalidate(const std::string& guid) {
  {
    std::lock_guard<std::mutex> l(mu_);
    ++epoch_[guid];
  }
  auto it = items_.find(guid);
  if (it == items_.end()) return;
  it->second.img.destroy();
  items_.erase(it);
}

void Thumbnails::clear() {
  std::vector<std::string> ids;
  for (auto& kv : items_) ids.push_back(kv.first);
  for (const std::string& g : ids) invalidate(g);
}

}  // namespace editor
}  // namespace rensga
