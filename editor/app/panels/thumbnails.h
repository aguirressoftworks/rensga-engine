// F3.02: thumbnails for the Project window and previews for the asset inspector.
// PNGs are decoded on a worker thread (the cooker's decodePng, which is stb_image) and shrunk to a small RGBA tile; the
// main thread uploads the tile to a GL texture. The texture cache is bounded (LRU): the oldest tiles are freed first.
#pragma once
#include <imgui.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rensga {
namespace editor {

// A GL texture made from RGBA8 pixels (main thread only).
struct GlImage {
  unsigned tex = 0;
  int w = 0, h = 0;
  void upload(const uint8_t* rgba, int width, int height, bool nearest);
  void destroy();
  ImTextureID id() const { return static_cast<ImTextureID>(tex); }
};

class Thumbnails {
 public:
  Thumbnails() = default;
  ~Thumbnails();
  // Texture id of the tile for a PNG asset; 0 while it decodes (or when it cannot be decoded). Queues the decode.
  ImTextureID get(const std::string& guid, const std::filesystem::path& png);
  // Uploads finished tiles and frees the oldest textures past the cap. Call once per frame on the main thread.
  void pump();
  void invalidate(const std::string& guid);
  void clear();
  size_t cached() const { return items_.size(); }  // GL textures alive
  size_t cap = 160;

 private:
  struct Item { GlImage img; bool requested = false, failed = false; uint64_t used = 0; };
  struct Req { std::string guid; std::filesystem::path path; int epoch = 0; };
  struct Done { std::string guid; int epoch = 0; std::vector<uint8_t> rgba; int w = 0, h = 0; bool ok = false; };
  void worker();
  void startThread();

  std::map<std::string, Item> items_;
  std::deque<Req> queue_;
  std::vector<Done> done_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::thread thread_;
  bool stop_ = false, started_ = false;
  uint64_t tick_ = 0;
  std::map<std::string, int> epoch_;  // bumped by invalidate so late results of an old file are dropped
};

}  // namespace editor
}  // namespace rensga
