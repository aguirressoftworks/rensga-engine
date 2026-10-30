// MVP-40: the one seam between the editor and a console. editor/core and tools/pipeline never name
// a console; each console is a TargetModule in editor/targets/<console> that self-registers.
// No ImGui here: the CLI uses it too.
#pragma once
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rensga/cooker/cooker.h"
#include "rensga/cooker/plugin.h"
#include "rensga/pipeline/emulator_launcher.h"
#include "rensga/pipeline/pipeline.h"

namespace rensga {
namespace editor {

using pipeline::EventSink;

struct BuildContext {
  std::filesystem::path project;     // project root (Assets/, ProjectSettings/, Library/)
  std::filesystem::path cookedPack;  // Library/Cooked/<target>/game.rpak
  std::filesystem::path buildDir;    // Library/Build/<target>-dev or -release
  bool release = false;
  int quitAfterSeconds = 0;
  std::filesystem::path generatedDir;       // Library/Generated/<target>/ (console_target.gen.h)
  const std::atomic<bool>* cancel = nullptr;  // set by Session::stop(); long steps poll it
};

struct PreviewRules {  // viewport rules from the ConsoleTarget
  int width = 0;
  int height = 0;
};

struct InspectorRules {
  std::vector<std::string> allowedComponents;  // component type names; others are greyed out with a reason
};

// F3.02 (Editor 3): the VRAM map. The editor lists what a target keeps in video memory (VramItem, from the ConsoleTarget
// and the cooked texture sizes); a TargetModule may place the items the way its hardware does.
struct VramItem {
  std::string name;   // "Draw buffer 0", "Depth buffer", an asset path
  std::string kind;   // "framebuffer" | "depth" | "texture" | "palette"
  std::string guid;   // asset GUID for textures and palettes, else empty
  uint64_t bytes = 0;
  uint32_t width = 0, height = 0;  // pixels (informational)
  uint32_t bits = 0;               // bits per pixel of the stored data
};
struct VramPlacement {
  VramItem item;
  uint64_t offset = 0;  // byte offset in VRAM
  uint64_t size = 0;    // bytes the item occupies, including padding
  bool outside = false; // starts or ends beyond the capacity
};
struct VramLayout {
  uint64_t capacity = 0;  // ConsoleTarget budgets.vramBytes
  uint64_t used = 0;      // end of the last placed item
  std::vector<VramPlacement> items;
  bool overBudget() const { return used > capacity; }
};
// Generic layout: items in order, each aligned to `align` bytes. A module that has real rules calls its own placer.
VramLayout linearVramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items, uint64_t align = 256);
// The items every target has from its ConsoleTarget: the color buffers and, with a z-buffer, the depth buffer.
std::vector<VramItem> framebufferVramItems(const ConsoleTargetData& ct);

class TargetModule {
 public:
  virtual ~TargetModule() = default;
  virtual std::string_view id() const = 0;           // short target id
  virtual std::string_view displayName() const = 0;  // short product name shown in the toolbar
  virtual ICookerFormatPlugin& cookerPlugin() = 0;
  // Rules are data: by default they come from the project's ConsoleTarget.json (F1.04). A module
  // overrides them only to add rules the file cannot express.
  virtual PreviewRules previewRules(const ConsoleTargetData& ct) const { return PreviewRules{static_cast<int>(ct.width), static_cast<int>(ct.height)}; }
  virtual InspectorRules inspectorRules(const ConsoleTargetData& ct) const { return InspectorRules{ct.allowedComponents}; }
  // F3.02: where the items sit in video memory. The default is the generic linear layout.
  virtual VramLayout vramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items) const { return linearVramLayout(ct, items); }
  virtual bool validate(const BuildContext& ctx, EventSink sink) = 0;
  virtual bool compile(const BuildContext& ctx, EventSink sink) = 0;
  virtual bool package(const BuildContext& ctx, EventSink sink) = 0;
  virtual pipeline::EmulatorLauncher& launcher() = 0;
};

class TargetRegistry {
 public:
  static const std::vector<TargetModule*>& all();
  static TargetModule* find(std::string_view id);
  static void add(TargetModule* module);  // called by RENSGA_REGISTER_TARGET
};

// ConsoleTarget.json of a project, cached by file mtime (cheap enough to call every frame).
// Returns a default-constructed (zero) ConsoleTargetData when the file is missing or invalid.
const ConsoleTargetData& consoleTargetFor(const std::filesystem::path& projectRoot);

struct TargetRegistrar {
  explicit TargetRegistrar(TargetModule* m) { TargetRegistry::add(m); }
};

}  // namespace editor
}  // namespace rensga

// In the target module's .cpp. The library must be linked with $<LINK_LIBRARY:WHOLE_ARCHIVE,...>.
#define RENSGA_REGISTER_TARGET(Type)                                                  \
  static Type g_target_module_##Type;                                                 \
  static ::rensga::editor::TargetRegistrar g_target_registrar_##Type(&g_target_module_##Type)
