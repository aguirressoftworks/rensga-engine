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

class TargetModule {
 public:
  virtual ~TargetModule() = default;
  virtual std::string_view id() const = 0;           // short target id
  virtual std::string_view displayName() const = 0;  // short product name shown in the toolbar
  virtual ICookerFormatPlugin& cookerPlugin() = 0;
  virtual PreviewRules previewRules() const = 0;
  virtual InspectorRules inspectorRules() const = 0;
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

struct TargetRegistrar {
  explicit TargetRegistrar(TargetModule* m) { TargetRegistry::add(m); }
};

}  // namespace editor
}  // namespace rensga

// In the target module's .cpp. The library must be linked with $<LINK_LIBRARY:WHOLE_ARCHIVE,...>.
#define RENSGA_REGISTER_TARGET(Type)                                                  \
  static Type g_target_module_##Type;                                                 \
  static ::rensga::editor::TargetRegistrar g_target_registrar_##Type(&g_target_module_##Type)
