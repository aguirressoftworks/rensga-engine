#include "rensga/editor/vram_map.h"

namespace rensga {
namespace editor {

std::vector<VramItem> collectVramItems(const ConsoleTargetData& ct, AssetSync& sync) {
  std::vector<VramItem> items = framebufferVramItems(ct);
  sync.importMissing();
  for (const AssetEntry& e : sync.db().entries()) {
    if (e.kind != AssetKind::Texture) continue;
    const ImportOutcome* o = sync.outcome(e.guid);
    if (!o || !o->ok) continue;
    VramItem t;
    t.name = e.rel;
    t.kind = "texture";
    t.guid = e.guid;
    t.bytes = o->tex.pixels.size();
    t.width = o->tex.width;
    t.height = o->tex.height;
    t.bits = o->tex.format == 0 ? 32 : (o->tex.format == 1 ? 4 : 8);
    items.push_back(t);
    if (!o->tex.palette.empty()) {
      VramItem p;
      p.name = e.rel + " (palette)";
      p.kind = "palette";
      p.guid = e.guid;
      p.bytes = o->tex.palette.size() * 4;
      p.width = static_cast<uint32_t>(o->tex.palette.size());
      p.height = 1;
      p.bits = 32;
      items.push_back(p);
    }
  }
  return items;
}

VramLayout layoutVram(const TargetModule* module, const ConsoleTargetData& ct, const std::vector<VramItem>& items) {
  return module ? module->vramLayout(ct, items) : linearVramLayout(ct, items);
}

}  // namespace editor
}  // namespace rensga
