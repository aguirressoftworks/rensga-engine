// F6.04 Editor 10: the Input Actions window. Edits ProjectSettings/InputActions.json (the file the cooker already reads): actions,
// their kind (button, axis, 2D axis), the virtual-gamepad source (pad buttons and sticks), keyboard bindings and the tuning values.
// Validation is the cooker's own, so a green window means a green cook. Touch needs no extra binding (see the note in the window).
//
// --ui-event verbs (output lines start with "input:"):
//   inputactions[:off]            open / close the window
//   iaadd:<button|axis|axis2>[|name]   add an action        iadel:<name>        remove       iamove:<name>|<index>   reorder
//   iaset:<name>|<field>=<value>  field: name kind source alt up down left right positive negative (comma lists, e.g. key:W,up)
//                                 gravity sensitivity dead snap invert; also deadzone=<n> outerclamp=<n> with the name "_"
//   iasave | iarevert | iaundo | iaredo
//   iadump                        every action as the file would store it, and the validation result
#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <sstream>

#include "app.h"
#include "panel_registry.h"
#include "rensga/editor/input_actions_doc.h"

namespace rensga {
namespace editor {

namespace {

struct IaState {
  bool open = false;
  bool loadedFor = false;
  std::string root;
  InputActionsDoc doc, saved;
  std::vector<nlohmann::json> history;
  int histPos = -1;
  int sel = 0;
  std::string error;
  char nameBuf[32] = {};
  int nameFor = -2;
};
IaState g_ia;

void snapshot() {
  g_ia.history.resize(static_cast<size_t>(g_ia.histPos + 1));
  g_ia.history.push_back(g_ia.doc.toJson());
  g_ia.histPos = static_cast<int>(g_ia.history.size()) - 1;
  if (g_ia.history.size() > 200) { g_ia.history.erase(g_ia.history.begin()); --g_ia.histPos; }
}

void reload(App& a) {
  std::string err;
  g_ia.root = a.project.root.string();
  if (!loadInputActions(a.project.root, g_ia.doc, err)) { g_ia.error = err; g_ia.doc = InputActionsDoc(); }
  g_ia.saved = g_ia.doc;
  g_ia.history.clear();
  g_ia.histPos = -1;
  snapshot();
  g_ia.sel = 0;
  g_ia.nameFor = -2;
  g_ia.loadedFor = true;
}

bool dirty() { return g_ia.doc.toJson() != g_ia.saved.toJson(); }

void undoRedo(int dir) {
  const int p = g_ia.histPos + dir;
  if (p < 0 || p >= static_cast<int>(g_ia.history.size())) return;
  std::string err;
  g_ia.doc.fromJson(g_ia.history[static_cast<size_t>(p)], err);
  g_ia.histPos = p;
  if (g_ia.sel >= static_cast<int>(g_ia.doc.actions.size())) g_ia.sel = static_cast<int>(g_ia.doc.actions.size()) - 1;
  g_ia.nameFor = -2;
}

// One binding slot: [Pad | Key] type and the value. Returns true when changed. `padChoices` lists the pad sources valid here.
bool bindingSlot(const char* id, std::string& v, const std::vector<InputChoice>& padChoices, bool allowEmpty) {
  bool changed = false;
  ImGui::PushID(id);
  const bool isKey = v.size() > 4 && (v.compare(0, 4, "key:") == 0);
  int type = v.empty() ? 0 : (isKey ? 2 : 1);   // 0 none, 1 pad, 2 key
  const char* types[] = {"(none)", "Pad", "Key"};
  ImGui::SetNextItemWidth(78);
  if (ImGui::BeginCombo("##type", types[type])) {
    for (int t = (allowEmpty ? 0 : 1); t < 3; ++t)
      if (ImGui::Selectable(types[t], t == type) && t != type) {
        v = t == 0 ? std::string() : (t == 1 ? std::string(padChoices.empty() ? "south" : padChoices[0].id) : std::string("key:Space"));
        changed = true;
      }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(190);
  if (type == 1) {
    if (ImGui::BeginCombo("##pad", inputBindingLabel(v).c_str())) {
      for (const InputChoice& c : padChoices)
        if (ImGui::Selectable(c.label, v == c.id)) { v = c.id; changed = true; }
      ImGui::EndCombo();
    }
  } else if (type == 2) {
    if (ImGui::BeginCombo("##key", v.substr(4).c_str())) {
      for (const std::string& k : inputKeyNames())
        if (ImGui::Selectable(k.c_str(), v.substr(4) == k)) { v = "key:" + k; changed = true; }
      ImGui::EndCombo();
    }
  } else {
    ImGui::BeginDisabled();
    ImGui::InputText("##none", const_cast<char*>(""), 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
  }
  ImGui::PopID();
  return changed;
}

// A role with `slots` bindings (alt / up / ...).
bool bindingRole(const char* label, std::vector<std::string>& list, int slots, const std::vector<InputChoice>& padChoices) {
  bool changed = false;
  if (static_cast<int>(list.size()) < slots) list.resize(static_cast<size_t>(slots));
  for (int i = 0; i < slots; ++i) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(i == 0 ? label : "");
    ImGui::SameLine(110);
    char id[32];
    std::snprintf(id, sizeof id, "%s%d", label, i);
    changed |= bindingSlot(id, list[static_cast<size_t>(i)], padChoices, true);
  }
  while (!list.empty() && list.back().empty()) list.pop_back();
  return changed;
}

void drawInputActions(App& a) {
  if (!g_ia.open || !a.loaded) return;
  if (!g_ia.loadedFor || g_ia.root != a.project.root.string()) reload(a);
  ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_Appearing);
  if (!ImGui::Begin("Input Actions", &g_ia.open)) { ImGui::End(); return; }
  bool changed = false;
  InputActionsDoc& d = g_ia.doc;
  const bool isDirty = dirty();
  if (ImGui::Button("Save")) { std::string err; if (saveInputActions(a.project.root, d, err)) { g_ia.saved = d; g_ia.error.clear(); EditorLog::instance().info("saved ProjectSettings/InputActions.json"); } else g_ia.error = err; }
  ImGui::SameLine();
  if (ImGui::Button("Revert")) reload(a);
  ImGui::SameLine();
  ImGui::BeginDisabled(g_ia.histPos <= 0);
  if (ImGui::Button("Undo")) undoRedo(-1);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(g_ia.histPos + 1 >= static_cast<int>(g_ia.history.size()));
  if (ImGui::Button("Redo")) undoRedo(1);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextColored(isDirty ? brandToken("warn") : brandToken("text-2"), isDirty ? "unsaved changes" : "saved");
  ImGui::SameLine(0, 24);
  ImGui::SetNextItemWidth(90);
  float dz = static_cast<float>(d.stickDeadZone), oc = static_cast<float>(d.stickOuterClamp);
  if (ImGui::DragFloat("Stick dead zone", &dz, 0.005f, 0.0f, 0.9f, "%.2f")) { d.stickDeadZone = dz; changed = true; }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  if (ImGui::DragFloat("Outer clamp", &oc, 0.005f, 0.1f, 1.0f, "%.2f")) { d.stickOuterClamp = oc; changed = true; }
  ImGui::Separator();

  // list
  ImGui::BeginChild("##list", ImVec2(210, -96), true);
  for (int i = 0; i < static_cast<int>(d.actions.size()); ++i) {
    char row[96];
    std::snprintf(row, sizeof row, "%s  [%s]", d.actions[static_cast<size_t>(i)].name.c_str(), d.actions[static_cast<size_t>(i)].kind.c_str());
    if (ImGui::Selectable(row, g_ia.sel == i)) { g_ia.sel = i; g_ia.nameFor = -2; }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##detail", ImVec2(0, -96), true);
  if (g_ia.sel >= 0 && g_ia.sel < static_cast<int>(d.actions.size())) {
    InputActionEntry& e = d.actions[static_cast<size_t>(g_ia.sel)];
    if (g_ia.nameFor != g_ia.sel) { std::snprintf(g_ia.nameBuf, sizeof g_ia.nameBuf, "%s", e.name.c_str()); g_ia.nameFor = g_ia.sel; }
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Name", g_ia.nameBuf, sizeof g_ia.nameBuf, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
      if (d.rename(g_ia.sel, g_ia.nameBuf)) changed = true; else std::snprintf(g_ia.nameBuf, sizeof g_ia.nameBuf, "%s", e.name.c_str());
    }
    const char* kinds[] = {"button", "axis", "axis2"};
    const char* kindLabels[] = {"Button", "Axis (-1..1)", "2D Axis (stick or keys)"};
    int k = e.kind == "axis" ? 1 : (e.kind == "axis2" ? 2 : 0);
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("Kind", kindLabels[k])) {
      for (int i = 0; i < 3; ++i) if (ImGui::Selectable(kindLabels[i], i == k) && i != k) { d.setKind(g_ia.sel, kinds[i]); changed = true; }
      ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Gamepad");
    const std::vector<InputChoice>& srcs = e.kind == "button" ? inputButtonSources() : (e.kind == "axis" ? inputAxisSources() : inputAxis2Sources());
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("Source", inputBindingLabel(e.source).c_str())) {
      for (const InputChoice& c : srcs)
        if (ImGui::Selectable(c.label, e.source == c.id)) { e.source = c.id; changed = true; }
      if (e.kind == "button") {
        ImGui::Separator();
        ImGui::TextDisabled("Keyboard key");
        for (const std::string& key : inputKeyNames())
          if (ImGui::Selectable(("Key " + key).c_str(), e.source == "key:" + key)) { e.source = "key:" + key; changed = true; }
      }
      ImGui::EndCombo();
    }
    ImGui::SeparatorText("Keyboard and extra bindings");
    if (e.kind == "button") {
      changed |= bindingRole("Alternative", e.alt, 1, inputButtonSources());
    } else if (e.kind == "axis2") {
      if (e.source != "composite") ImGui::TextDisabled("Extra keys apply when the source is \"Keys / buttons (composite)\"; they are still saved.");
      changed |= bindingRole("Up", e.up, 2, inputButtonSources());
      changed |= bindingRole("Down", e.down, 2, inputButtonSources());
      changed |= bindingRole("Left", e.left, 2, inputButtonSources());
      changed |= bindingRole("Right", e.right, 2, inputButtonSources());
    } else {
      if (e.source != "composite") ImGui::TextDisabled("Positive / negative bindings apply to the composite source; they are still saved.");
      changed |= bindingRole("Positive", e.positive, 2, inputButtonSources());
      changed |= bindingRole("Negative", e.negative, 2, inputButtonSources());
    }
    ImGui::SeparatorText("Tuning");
    float gr = static_cast<float>(e.gravity), se = static_cast<float>(e.sensitivity), de = static_cast<float>(e.dead);
    ImGui::SetNextItemWidth(120);
    if (ImGui::DragFloat("Gravity", &gr, 0.05f, 0.0f, 100.0f)) { e.gravity = gr; changed = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::DragFloat("Sensitivity", &se, 0.05f, 0.01f, 100.0f)) { e.sensitivity = se; changed = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::DragFloat("Dead", &de, 0.005f, 0.0f, 0.99f)) { e.dead = de; changed = true; }
    if (ImGui::Checkbox("Snap", &e.snap)) changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Invert", &e.invert)) changed = true;
  } else {
    ImGui::TextDisabled("No action selected. Add one with +.");
  }
  ImGui::EndChild();

  // footer: add / remove / move, validation, touch note
  if (ImGui::Button("+ Button")) { g_ia.sel = d.add("button"); changed = true; }
  ImGui::SameLine();
  if (ImGui::Button("+ Axis")) { g_ia.sel = d.add("axis"); changed = true; }
  ImGui::SameLine();
  if (ImGui::Button("+ 2D Axis")) { g_ia.sel = d.add("axis2"); changed = true; }
  ImGui::SameLine();
  if (ImGui::Button("Remove") && d.remove(g_ia.sel)) { if (g_ia.sel >= static_cast<int>(d.actions.size())) g_ia.sel = static_cast<int>(d.actions.size()) - 1; g_ia.nameFor = -2; changed = true; }
  ImGui::SameLine();
  if (ImGui::Button("Up") && d.move(g_ia.sel, g_ia.sel - 1)) { --g_ia.sel; changed = true; }
  ImGui::SameLine();
  if (ImGui::Button("Down") && d.move(g_ia.sel, g_ia.sel + 1)) { ++g_ia.sel; changed = true; }
  const std::vector<std::string> problems = d.validate();
  if (problems.empty() && g_ia.error.empty()) ImGui::TextColored(brandToken("ok"), "OK: the cooker accepts these %zu actions.", d.actions.size());
  for (const std::string& p : problems) ImGui::TextColored(brandToken("danger"), "Error: %s", p.c_str());
  if (!g_ia.error.empty()) ImGui::TextColored(brandToken("danger"), "Error: %s", g_ia.error.c_str());
  ImGui::TextDisabled("%s", inputTouchNote());
  if (changed) snapshot();
  ImGui::End();
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t at = 0;
  while (true) { size_t e = s.find(sep, at); out.push_back(s.substr(at, e == std::string::npos ? e : e - at)); if (e == std::string::npos) break; at = e + 1; }
  return out;
}

std::vector<std::string> listOf(const std::string& v) {
  std::vector<std::string> out;
  if (v.empty() || v == "none") return out;
  return split(v, ',');
}

bool inputHook(App& a, const std::string& act, const std::string& arg) {
  auto ensure = [&] { if (!g_ia.loadedFor || g_ia.root != a.project.root.string()) reload(a); };
  if (act == "inputactions") {
    g_ia.open = arg != "off";
    if (g_ia.open) ensure();
    std::printf("input: window %s\n", g_ia.open ? "open" : "closed");
  } else if (act == "iaadd") {
    ensure();
    const std::vector<std::string> p = split(arg, '|');
    const int i = g_ia.doc.add(p[0]);
    if (p.size() > 1) g_ia.doc.rename(i, p[1]);
    g_ia.sel = i;
    g_ia.nameFor = -2;
    snapshot();
    std::printf("input: added '%s' (%s), %zu actions\n", g_ia.doc.actions[static_cast<size_t>(i)].name.c_str(), g_ia.doc.actions[static_cast<size_t>(i)].kind.c_str(), g_ia.doc.actions.size());
  } else if (act == "iadel") {
    ensure();
    const int i = g_ia.doc.indexOf(arg);
    const bool ok = g_ia.doc.remove(i);
    if (g_ia.sel >= static_cast<int>(g_ia.doc.actions.size())) g_ia.sel = static_cast<int>(g_ia.doc.actions.size()) - 1;
    g_ia.nameFor = -2;
    if (ok) snapshot();
    std::printf("input: remove '%s' %s, %zu actions\n", arg.c_str(), ok ? "ok" : "refused", g_ia.doc.actions.size());
  } else if (act == "iamove") {
    ensure();
    const std::vector<std::string> p = split(arg, '|');
    const bool ok = p.size() > 1 && g_ia.doc.move(g_ia.doc.indexOf(p[0]), std::atoi(p[1].c_str()));
    if (ok) snapshot();
    std::printf("input: move '%s' %s\n", p[0].c_str(), ok ? "ok" : "refused");
  } else if (act == "iaset") {
    ensure();
    const std::vector<std::string> p = split(arg, '|');
    const size_t eq = p.size() > 1 ? p[1].find('=') : std::string::npos;
    if (eq == std::string::npos) { std::printf("input: iaset needs <name>|<field>=<value>\n"); std::fflush(stdout); return true; }
    const std::string f = p[1].substr(0, eq), v = p[1].substr(eq + 1);
    InputActionsDoc& d = g_ia.doc;
    bool ok = true;
    if (p[0] == "_") {
      if (f == "deadzone") d.stickDeadZone = std::atof(v.c_str()); else if (f == "outerclamp") d.stickOuterClamp = std::atof(v.c_str()); else ok = false;
    } else {
      const int i = d.indexOf(p[0]);
      if (i < 0) ok = false;
      else {
        InputActionEntry& e = d.actions[static_cast<size_t>(i)];
        if (f == "name") ok = d.rename(i, v);
        else if (f == "kind") ok = d.setKind(i, v);
        else if (f == "source") e.source = v;
        else if (f == "alt") e.alt = listOf(v);
        else if (f == "up") e.up = listOf(v);
        else if (f == "down") e.down = listOf(v);
        else if (f == "left") e.left = listOf(v);
        else if (f == "right") e.right = listOf(v);
        else if (f == "positive") e.positive = listOf(v);
        else if (f == "negative") e.negative = listOf(v);
        else if (f == "gravity") e.gravity = std::atof(v.c_str());
        else if (f == "sensitivity") e.sensitivity = std::atof(v.c_str());
        else if (f == "dead") e.dead = std::atof(v.c_str());
        else if (f == "snap") e.snap = v == "true" || v == "1";
        else if (f == "invert") e.invert = v == "true" || v == "1";
        else ok = false;
      }
    }
    if (ok) { g_ia.nameFor = -2; snapshot(); }
    std::printf("input: set %s.%s %s\n", p[0].c_str(), f.c_str(), ok ? "ok" : "refused");
  } else if (act == "iasave") {
    ensure();
    std::string err;
    const bool ok = saveInputActions(a.project.root, g_ia.doc, err);
    if (ok) g_ia.saved = g_ia.doc;
    std::printf("input: save %s\n", ok ? "ok" : ("failed: " + err).c_str());
  } else if (act == "iarevert") {
    reload(a);
    std::printf("input: reverted, %zu actions\n", g_ia.doc.actions.size());
  } else if (act == "iaundo" || act == "iaredo") {
    ensure();
    undoRedo(act == "iaundo" ? -1 : 1);
    std::printf("input: %s, %zu actions\n", act.c_str() + 2, g_ia.doc.actions.size());
  } else if (act == "iadump") {
    ensure();
    const std::vector<std::string> problems = g_ia.doc.validate();
    std::printf("input: %zu actions dirty=%d problems=%zu\n", g_ia.doc.actions.size(), dirty() ? 1 : 0, problems.size());
    for (const InputActionEntry& e : g_ia.doc.actions) {
      std::string line = "input:   " + e.name + " kind=" + e.kind + " source=" + e.source;
      auto add = [&](const char* n, const std::vector<std::string>& v) { if (v.empty()) return; line += std::string(" ") + n + "="; for (size_t i = 0; i < v.size(); ++i) line += (i ? "," : "") + v[i]; };
      add("alt", e.alt); add("up", e.up); add("down", e.down); add("left", e.left); add("right", e.right); add("positive", e.positive); add("negative", e.negative);
      std::printf("%s\n", line.c_str());
    }
    for (const std::string& p : problems) std::printf("input:   problem: %s\n", p.c_str());
  } else {
    return false;
  }
  std::fflush(stdout);
  return true;
}

}  // namespace

RENSGA_EDITOR_PANEL(input_actions, "Input Actions", drawInputActions, inputHook, nullptr, 670, 96, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor
}  // namespace rensga
