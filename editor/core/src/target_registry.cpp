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
