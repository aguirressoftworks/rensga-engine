// Fixture of rensga_extension_tests: an Editor/ module with one of each registration. No ImGui here (the test executable has none), so the
// callbacks only use the editor API.
#include <rensga/editor/extension.h>

using namespace rensga::editor::ext;

static int g_calls = 0;

static void inspector(ComponentView& v) {
  ++g_calls;
  v.SetInt("count", v.GetInt("count") + 1);  // an undo-aware edit made from the module
}
static void window() { Log::Info("ext-fixture: window drawn"); }
static void menu() {
  Log::Info("ext-fixture: menu item v1 calls=" + std::to_string(g_calls));
  Selection::Select("B");
}

RENSGA_CUSTOM_INSPECTOR(ExtProbe, inspector)
RENSGA_CUSTOM_INSPECTOR_REPLACE(ExtOther, inspector)
RENSGA_EDITOR_WINDOW("Fixture Window", window)
RENSGA_MENU_ITEM("Tools/Fixture Item", menu)
RENSGA_MENU_ITEM_KEY("Tools/Fixture Key", "Ctrl+Shift+F", menu)
