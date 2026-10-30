#include "rensga/editor/target_module.h"

#include <system_error>

namespace rensga {
namespace editor {

static std::vector<TargetModule*>& modules() {
  static std::vector<TargetModule*> m;
  return m;
}

const std::vector<TargetModule*>& TargetRegistry::all() { return modules(); }
void TargetRegistry::add(TargetModule* module) { modules().push_back(module); }
TargetModule* TargetRegistry::find(std::string_view id) {
  for (TargetModule* m : modules())
    if (m->id() == id) return m;
  return nullptr;
}

std::vector<VramItem> framebufferVramItems(const ConsoleTargetData& ct) {
  std::vector<VramItem> out;
  const uint64_t fb = static_cast<uint64_t>(ct.width) * ct.height * ct.fbBpp / 8;
  for (uint32_t i = 0; i < ct.fbBuffers; ++i) {
    VramItem it;
    it.name = "Color buffer " + std::to_string(i);
    it.kind = "framebuffer";
    it.bytes = fb;
    it.width = ct.width;
    it.height = ct.height;
    it.bits = ct.fbBpp;
    out.push_back(it);
  }
  if (ct.depth == "zbuffer" && ct.fbDepthBits) {
    VramItem it;
    it.name = "Depth buffer";
    it.kind = "depth";
    it.bytes = static_cast<uint64_t>(ct.width) * ct.height * ct.fbDepthBits / 8;
    it.width = ct.width;
    it.height = ct.height;
    it.bits = ct.fbDepthBits;
    out.push_back(it);
  }
  return out;
}

VramLayout linearVramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items, uint64_t align) {
  VramLayout l;
  l.capacity = ct.vramBytes;
  uint64_t at = 0;
  for (const VramItem& it : items) {
    if (align) at = (at + align - 1) / align * align;
    VramPlacement p;
    p.item = it;
    p.offset = at;
    p.size = it.bytes;
    at += it.bytes;
    p.outside = at > l.capacity;
    l.items.push_back(std::move(p));
  }
  l.used = at;
  return l;
}

const ConsoleTargetData& consoleTargetFor(const std::filesystem::path& projectRoot) {
  static std::filesystem::path cachedRoot;
  static std::filesystem::file_time_type cachedTime;
  static ConsoleTargetData cached;
  static bool valid = false;
  const std::filesystem::path file = projectRoot / "ProjectSettings" / "ConsoleTarget.json";
  std::error_code ec;
  const auto mt = std::filesystem::last_write_time(file, ec);
  if (valid && cachedRoot == projectRoot && (ec || mt == cachedTime)) return cached;
  cachedRoot = projectRoot;
  cachedTime = ec ? std::filesystem::file_time_type() : mt;
  valid = true;
  cached = ConsoleTargetData();
  if (!ec) {
    try { cached = loadConsoleTarget(file); } catch (const std::exception&) {}
  }
  return cached;
}

}  // namespace editor
}  // namespace rensga
