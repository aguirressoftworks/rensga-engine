// F1.04: a second TargetModule registers and packages without any change to editor/core.
#include <filesystem>
#include <fstream>
#include <iterator>

#include "rensga/editor/target_module.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;

TEST(dummy_target_registers_and_packages_zip) {
  TargetModule* m = TargetRegistry::find("dummy");
  CHECK(m != nullptr);
  if (!m) return;
  CHECK_EQ(m->displayName(), std::string_view("Dummy"));
  CHECK(TargetRegistry::find("no-such-target") == nullptr);

  // Rules are data from the ConsoleTarget, not code in the module.
  rensga::ConsoleTargetData ct;
  ct.width = 320; ct.height = 240; ct.allowedComponents = {"Camera"};
  CHECK_EQ(m->previewRules(ct).width, 320);
  CHECK_EQ(m->previewRules(ct).height, 240);
  CHECK_EQ(m->inspectorRules(ct).allowedComponents.size(), 1u);

  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / "dummy_target";
  fs::remove_all(d);
  fs::create_directories(d);
  { std::ofstream(d / "game.rpak", std::ios::binary) << "RPAKDATA"; }
  BuildContext ctx;
  ctx.project = d;
  ctx.cookedPack = d / "game.rpak";
  ctx.buildDir = d / "out";
  CHECK(m->validate(ctx, nullptr));
  CHECK(m->package(ctx, nullptr));
  std::ifstream in(d / "out" / "dummy.zip", std::ios::binary);
  std::string z((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(z.size() > 30 + 8);
  CHECK(z.compare(0, 4, "PK\x03\x04", 4) == 0);
  CHECK(z.find("RPAKDATA") != std::string::npos);
}
