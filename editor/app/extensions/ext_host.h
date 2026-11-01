// F6.07: the application side of editor extensions (editor/core/extension.h): builds and hot-reloads <project>/Editor, draws the
// registered windows and menu items, runs custom inspectors. The panel registers itself (RENSGA_EDITOR_PANEL in ext_host.cpp).
#pragma once
#include <string>
#include <vector>

#include "app.h"
#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

// Custom inspectors registered for `type` by the user project (called by drawComponentInspectors). Returns how many ran.
int runExtensionInspectors(App& a, const std::string& type, const std::vector<ComponentRef>& refs, bool multi);
// A registered inspector replaces the generated fields of `type` (RENSGA_CUSTOM_INSPECTOR_REPLACE).
bool extensionReplacesFields(const std::string& type);
// Menu bar: the menus the project registered (Tools/..., any first path part), drawn before Help.
void drawExtensionMenus(App& a);
// Window menu: one checkbox per registered window, plus the "Editor Extensions" status window.
void drawExtensionWindowItems(App& a);

}  // namespace editor
}  // namespace rensga
