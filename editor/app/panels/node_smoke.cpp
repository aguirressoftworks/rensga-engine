// Hidden smoke window for the vendored imnodes (F6 E0): proves it links and draws. Opened only by --ui-event nodesmoke
// (and nodesmoke:off); the Animator and graph editors of Editor 8/9 build on the same library.
#include <imnodes.h>

#include <cstdio>

#include "app.h"
#include "panel_registry.h"

namespace rensga {
namespace editor {

namespace {
bool g_open = false;
ImNodesContext* g_ctx = nullptr;

void drawNodeSmoke(App&) {
  if (!g_open) return;
  if (!g_ctx) g_ctx = ImNodes::CreateContext();  // lives until exit (the ImGui context is destroyed first, so no DestroyContext)
  ImNodes::SetCurrentContext(g_ctx);
  ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_Appearing);
  if (ImGui::Begin("Node smoke test", &g_open)) {
    ImNodes::BeginNodeEditor();
    ImNodes::BeginNode(1);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Entry");
    ImNodes::EndNodeTitleBar();
    ImNodes::BeginOutputAttribute(2);
    ImGui::TextUnformatted("out");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
    ImNodes::BeginNode(3);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Idle");
    ImNodes::EndNodeTitleBar();
    ImNodes::BeginInputAttribute(4);
    ImGui::TextUnformatted("in");
    ImNodes::EndInputAttribute();
    ImNodes::EndNode();
    ImNodes::Link(5, 2, 4);
    ImNodes::EndNodeEditor();
  }
  ImGui::End();
}

bool nodeSmokeHook(App&, const std::string& act, const std::string& arg) {
  if (act != "nodesmoke") return false;
  g_open = arg != "off";
  std::printf("nodes: smoke window %s\n", g_open ? "open" : "closed");
  return true;
}
}  // namespace

RENSGA_EDITOR_PANEL(node_smoke, "Node smoke test", drawNodeSmoke, nodeSmokeHook, nullptr, 900, 130, ::rensga::editor::PanelDraw::Overlay)

}  // namespace editor
}  // namespace rensga
