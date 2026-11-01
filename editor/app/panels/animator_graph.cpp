// F6.03 (Editor 9): the Animator graph window. States are imnodes nodes, transitions are links (drag from a state's output pin to
// another state's input pin to add one), parameters live in a side panel, the selected state or transition is edited in the window's own
// inspector (motion, speed, exit time, duration, conditions). During Play in editor the running Animator's current state and
// cross-fade are highlighted live.
// --ui-event verbs: animatorgraph[:off] | animctl:<Assets/..controller.json> | animstate:add|<name> | animstate:rm|<name> |
// animstate:rename|<a>|<b> | animstate:default|<name> | animstate:motion|<name>|<clip asset path or guid> |
// animstate:opts|<name>|loop=0|speed=2|speedparam=<float param> | animparam:add|<name>|<float|int|bool|trigger>[|<default>] | animparam:rm|<name> |
// animtrans:<from or *>|<to>[|<cond>[;<cond>..]] (cond: speed>0.1 speed<0.1 n==3 n!=3 jump !grounded) | animtransset:<index>|exit=<t>|duration=<s> |
// animtransrm:<index> | animctlsave | animctlundo | animctlredo | animctlsel:state|<name> / animctlsel:trans|<index> | animctldump | animlive
#include <imnodes.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#include "app.h"
#include "panel_registry.h"
#include "rensga/core/animator_system.h"
#include "rensga/editor/anim_controller_doc.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr int kAnyNode = 90;
int nodeId(int state) { return 100 + state; }
int inPin(int state) { return 1000 + state * 2; }
int outPin(int state) { return 1001 + state * 2; }
constexpr int kAnyOut = 999;
int linkId(int trans) { return 5000 + trans; }

struct GraphWin {
  bool show = false;
  AnimControllerDoc doc;
  std::string rel, guid;      // asset path and GUID of the controller
  ImNodesContext* ctx = nullptr;
  std::string selState;
  int selTrans = -1;
  bool placed = false;        // node positions pushed into imnodes
  bool dragging = false;
  std::string status;
  char newState[48] = "NewState";
  char newParam[48] = "param";
  int newParamType = 0;
  // live view
  bool live = false;
  std::string liveState, liveNext;
  float liveTime = 0, liveBlend = 0;
};
GraphWin& G() { static GraphWin* g = new GraphWin(); return *g; }

// Where a new state goes: the next free cell of a 3-column grid (200 x 130 px), so new nodes never land on old ones.
float nextSlotX(const AnimControllerDoc& d) { return 60.0f + 200.0f * static_cast<float>(d.states().size() % 3); }
float nextSlotY(const AnimControllerDoc& d) { return 90.0f + 130.0f * static_cast<float>(d.states().size() / 3); }

std::string fmtNum(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.3f", v);
  std::string s = b;
  while (s.size() > 1 && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

std::string guidOfAsset(App& a, const std::string& rel) {
  std::ifstream in(a.project.root / (rel + ".meta"));
  if (!in) return std::string();
  json j = json::parse(in, nullptr, false);
  return j.is_object() ? j.value("guid", std::string()) : std::string();
}

bool looksLikeGuid(const std::string& s) { return s.size() == 36 && s[8] == '-' && s[13] == '-'; }

// Property clips of the project by GUID, rescanned every couple of seconds (drawing asks for it per state per frame).
std::string motionLabel(App& a, const std::string& guid) {
  static std::map<std::string, std::string> names;
  static double stamp = -100;
  if (ImGui::GetTime() - stamp > 2.0) {
    stamp = ImGui::GetTime();
    names.clear();
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(a.project.root / "Assets", ec)) {
      const std::string n = e.path().filename().string();
      if (n.size() > 10 && n.compare(n.size() - 10, 10, ".clip.json") == 0) {
        const std::string g = guidOfAsset(a, fs::relative(e.path(), a.project.root).generic_string());
        if (!g.empty()) names[g] = n;
      }
    }
  }
  if (guid.empty()) return "None";
  auto it = names.find(guid);
  return it != names.end() ? it->second : "(skeletal or missing) " + guid.substr(0, 8);
}

bool openController(App& a, const std::string& rel) {
  GraphWin& g = G();
  std::string err;
  AnimControllerDoc d;
  if (!d.load(a.project.root / rel, err)) { g.status = err; EditorLog::instance().error("animator: " + err); return false; }
  g.doc = d;
  g.rel = rel;
  g.guid = guidOfAsset(a, rel);
  g.show = true;
  g.placed = false;
  g.selState.clear();
  g.selTrans = -1;
  g.status.clear();
  return true;
}

// "speed>0.1", "n==3", "jump", "!grounded" -> a condition (false when the text is not one).
bool parseCond(const std::string& t, AnimCondDef& c) {
  size_t i = 0;
  if (!t.empty() && t[0] == '!') { c.mode = "ifNot"; c.param = t.substr(1); return !c.param.empty(); }
  while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_')) ++i;
  c.param = t.substr(0, i);
  if (c.param.empty()) return false;
  if (i == t.size()) { c.mode = "if"; return true; }
  std::string op;
  if (t.compare(i, 2, "==") == 0) { op = "equals"; i += 2; }
  else if (t.compare(i, 2, "!=") == 0) { op = "notEqual"; i += 2; }
  else if (t[i] == '>') { op = "greater"; ++i; }
  else if (t[i] == '<') { op = "less"; ++i; }
  else return false;
  c.mode = op;
  c.value = std::atof(t.c_str() + i);
  return true;
}

std::string condText(const AnimCondDef& c) {
  if (c.mode == "if") return c.param;
  if (c.mode == "ifNot") return "!" + c.param;
  const char* op = c.mode == "greater" ? ">" : (c.mode == "less" ? "<" : (c.mode == "equals" ? "==" : "!="));
  return c.param + op + fmtNum(c.value);
}

void refreshLive(App& a) {
  GraphWin& g = G();
  g.live = false;
  if (!a.session.active() || !a.session.world() || g.guid.empty() || !g.doc.loaded()) return;
  Animator* found = nullptr;
  char text[Guid::kTextLength + 1];
  a.session.world()->world().forEach<Animator>([&](Animator& an) {
    if (found) return;
    an.controller.format(text);
    if (g.guid == text) found = &an;
  });
  if (!found) return;
  AnimatorSystem::DebugInfo info;
  if (!AnimatorSystem::debugInfo(*found, info) || info.current >= g.doc.states().size()) return;
  g.live = true;
  g.liveState = g.doc.states()[info.current].name;
  g.liveNext = info.next < g.doc.states().size() ? g.doc.states()[info.next].name : std::string();
  g.liveTime = info.normalizedTime;
  g.liveBlend = info.blend;
}

void drawGraph(App& a, GraphWin& g) {
  ImNodes::SetCurrentContext(g.ctx);
  ImNodes::BeginNodeEditor();
  const auto& states = g.doc.states();
  const auto& trans = g.doc.transitions();
  bool anyUsed = false;
  for (const AnimTransDef& t : trans) anyUsed = anyUsed || t.from == "*";
  // Any State node
  ImNodes::PushColorStyle(ImNodesCol_TitleBar, ImGui::GetColorU32(brandToken("surface-2")));
  ImNodes::BeginNode(kAnyNode);
  ImNodes::BeginNodeTitleBar();
  ImGui::TextUnformatted("Any State");
  ImNodes::EndNodeTitleBar();
  ImNodes::BeginOutputAttribute(kAnyOut);
  ImGui::TextUnformatted("transitions >");
  ImNodes::EndOutputAttribute();
  ImNodes::EndNode();
  ImNodes::PopColorStyle();
  (void)anyUsed;
  for (size_t i = 0; i < states.size(); ++i) {
    const AnimStateDef& s = states[i];
    const bool isDefault = s.name == g.doc.defaultState();
    const bool isLive = g.live && s.name == g.liveState;
    const bool isNext = g.live && !g.liveNext.empty() && s.name == g.liveNext;
    ImU32 title = ImGui::GetColorU32(isLive ? brandToken("ok") : (isNext ? brandToken("info") : (isDefault ? brandToken("primary") : brandToken("surface-2"))));
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, title);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, title);
    if (isLive) ImNodes::PushColorStyle(ImNodesCol_NodeOutline, ImGui::GetColorU32(brandToken("ok")));
    ImNodes::BeginNode(nodeId(static_cast<int>(i)));
    ImNodes::BeginNodeTitleBar();
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken(isLive || isDefault ? "on-primary" : "text"));
    ImGui::TextUnformatted(s.name.c_str());
    ImGui::PopStyleColor();
    ImNodes::EndNodeTitleBar();
    ImNodes::BeginInputAttribute(inPin(static_cast<int>(i)));
    ImGui::TextDisabled("> in");
    ImNodes::EndInputAttribute();
    ImGui::TextDisabled("%s", motionLabel(a, s.motion).c_str());
    if (isDefault) ImGui::TextDisabled("default state");
    if (isLive) ImGui::TextColored(brandToken("ok"), "playing  t=%.2f", g.liveTime);
    if (isNext) ImGui::TextColored(brandToken("info"), "blending in %d%%", static_cast<int>(g.liveBlend * 100));
    ImNodes::BeginOutputAttribute(outPin(static_cast<int>(i)));
    ImGui::TextDisabled("out >");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
    if (isLive) ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
  }
  for (size_t i = 0; i < trans.size(); ++i) {
    const int from = trans[i].from == "*" ? -1 : g.doc.findState(trans[i].from), to = g.doc.findState(trans[i].to);
    if (to < 0 || (from < 0 && trans[i].from != "*")) continue;
    ImNodes::Link(linkId(static_cast<int>(i)), from < 0 ? kAnyOut : outPin(from), inPin(to));
  }
  ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
  ImNodes::EndNodeEditor();
  if (!g.placed) {
    ImNodes::SetNodeGridSpacePos(kAnyNode, ImVec2(-190, 90));
    for (size_t i = 0; i < states.size(); ++i) ImNodes::SetNodeGridSpacePos(nodeId(static_cast<int>(i)), ImVec2(states[i].x, states[i].y));
    g.placed = true;
  }
  // new transition
  int s = 0, e = 0;
  if (ImNodes::IsLinkCreated(&s, &e)) {
    const bool sAny = s == kAnyOut;
    const int sFrom = sAny ? -1 : (s - 1001) / 2;
    const int eTo = (e - 1000) / 2;
    if (e >= 1000 && e % 2 == 0 && (sAny || (s >= 1001 && s % 2 == 1)) && eTo < static_cast<int>(states.size()) && (sAny || sFrom < static_cast<int>(states.size()))) {
      const int t = g.doc.addTransition(sAny ? "*" : states[sFrom].name, states[eTo].name);
      if (t >= 0) { g.selTrans = t; g.selState.clear(); }
    }
  }
  // selection
  const int nSel = ImNodes::NumSelectedNodes(), lSel = ImNodes::NumSelectedLinks();
  if (nSel == 1) {
    int id;
    ImNodes::GetSelectedNodes(&id);
    if (id >= 100 && id - 100 < static_cast<int>(states.size())) { g.selState = states[id - 100].name; g.selTrans = -1; }
  } else if (lSel == 1) {
    int id;
    ImNodes::GetSelectedLinks(&id);
    g.selTrans = id - 5000;
    g.selState.clear();
  }
  // node dragging writes positions back (one undo step per drag)
  const bool mouseDown = ImGui::IsMouseDown(0);
  for (size_t i = 0; i < states.size(); ++i) {
    const ImVec2 p = ImNodes::GetNodeGridSpacePos(nodeId(static_cast<int>(i)));
    if (std::fabs(p.x - states[i].x) > 0.5f || std::fabs(p.y - states[i].y) > 0.5f) {
      if (mouseDown && !g.dragging) { g.doc.beginMove(); g.dragging = true; }
      g.doc.moveState(states[i].name, p.x, p.y);
    }
  }
  if (!mouseDown) g.dragging = false;
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput) {
    if (g.selTrans >= 0) { g.doc.removeTransition(g.selTrans); g.selTrans = -1; g.placed = true; }
    else if (!g.selState.empty()) { g.doc.removeState(g.selState); g.selState.clear(); g.placed = false; }
  }
}

void drawParams(GraphWin& g) {
  ImGui::TextUnformatted("Parameters");
  ImGui::Separator();
  const auto params = g.doc.params();
  for (size_t i = 0; i < params.size(); ++i) {
    const AnimParamDef& p = params[i];
    ImGui::PushID(static_cast<int>(i));
    static const char* tn[] = {"float", "int", "bool", "trigger"};
    ImGui::Text("%s", p.name.c_str());
    ImGui::SameLine(140);
    ImGui::TextDisabled("%s", tn[static_cast<int>(p.type)]);
    if (p.type != AnimParamType::Trigger) {
      ImGui::SameLine(190);
      ImGui::SetNextItemWidth(60);
      if (p.type == AnimParamType::Bool) {
        bool b = p.defaultValue != 0;
        if (ImGui::Checkbox("##def", &b)) g.doc.setParamDefault(p.name, b ? 1 : 0);
      } else {
        float v = static_cast<float>(p.defaultValue);
        ImGui::DragFloat("##def", &v, p.type == AnimParamType::Int ? 1.0f : 0.05f);
        if (ImGui::IsItemDeactivatedAfterEdit()) g.doc.setParamDefault(p.name, p.type == AnimParamType::Int ? std::round(v) : v);
      }
    }
    ImGui::SameLine(262);
    if (ImGui::SmallButton("x")) { g.doc.removeParam(p.name); ImGui::PopID(); break; }
    ImGui::PopID();
  }
  ImGui::SetNextItemWidth(120);
  ImGui::InputText("##np", g.newParam, sizeof g.newParam);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::Combo("##npt", &g.newParamType, "float\0int\0bool\0trigger\0");
  ImGui::SameLine();
  if (ImGui::Button("Add")) {
    if (g.doc.addParam(g.newParam, static_cast<AnimParamType>(g.newParamType)) < 0) g.status = "cannot add parameter '" + std::string(g.newParam) + "' (empty, taken, or 16 already)";
  }
}

void drawInspector(App& a, GraphWin& g) {
  ImGui::TextUnformatted("Inspector");
  ImGui::Separator();
  if (!g.selState.empty() && g.doc.findState(g.selState) >= 0) {
    const AnimStateDef s = g.doc.states()[g.doc.findState(g.selState)];
    ImGui::Text("State  %s", s.name.c_str());
    char nm[48];
    std::snprintf(nm, sizeof nm, "%s", s.name.c_str());
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputText("name", nm, sizeof nm, ImGuiInputTextFlags_EnterReturnsTrue) && g.doc.renameState(s.name, nm)) g.selState = nm;
    // motion picker: every property clip of the project
    if (ImGui::BeginCombo("motion", motionLabel(a, s.motion).c_str())) {
      if (ImGui::Selectable("None", s.motion.empty())) g.doc.setMotion(s.name, "");
      std::error_code ec;
      for (const auto& e : fs::recursive_directory_iterator(a.project.root / "Assets", ec)) {
        const std::string n = e.path().filename().string();
        if (n.size() > 10 && n.compare(n.size() - 10, 10, ".clip.json") == 0) {
          const std::string rel = fs::relative(e.path(), a.project.root).generic_string();
          const std::string gd = guidOfAsset(a, rel);
          if (ImGui::Selectable(rel.c_str(), gd == s.motion)) g.doc.setMotion(s.name, gd);
        }
      }
      ImGui::EndCombo();
    }
    bool loop = s.loop;
    float speed = static_cast<float>(s.speed);
    bool ch = ImGui::Checkbox("loop", &loop);
    ImGui::SetNextItemWidth(80);
    ImGui::DragFloat("speed", &speed, 0.02f, 0.0f, 20.0f);
    ch = ch || ImGui::IsItemDeactivatedAfterEdit();
    std::string sp = s.speedParameter;
    if (ImGui::BeginCombo("speed parameter", sp.empty() ? "None" : sp.c_str())) {
      if (ImGui::Selectable("None", sp.empty())) { sp.clear(); ch = true; }
      for (const AnimParamDef& p : g.doc.params())
        if (p.type == AnimParamType::Float && ImGui::Selectable(p.name.c_str(), p.name == sp)) { sp = p.name; ch = true; }
      ImGui::EndCombo();
    }
    if (ch) g.doc.setStateOptions(s.name, loop, speed, sp);
    if (s.name != g.doc.defaultState() && ImGui::Button("Set as default")) g.doc.setDefaultState(s.name);
    ImGui::SameLine();
    if (ImGui::Button("Delete state")) { g.doc.removeState(s.name); g.selState.clear(); g.placed = false; }
    return;
  }
  if (g.selTrans >= 0 && g.selTrans < static_cast<int>(g.doc.transitions().size())) {
    const AnimTransDef t = g.doc.transitions()[g.selTrans];
    ImGui::Text("Transition  %s  ->  %s", t.from == "*" ? "Any State" : t.from.c_str(), t.to.c_str());
    bool exit = t.hasExitTime;
    float et = static_cast<float>(t.exitTime), dur = static_cast<float>(t.duration);
    bool ch = ImGui::Checkbox("has exit time", &exit);
    ImGui::BeginDisabled(!exit);
    ImGui::SetNextItemWidth(80);
    ImGui::DragFloat("exit time", &et, 0.01f, 0.0f, 100.0f);
    ch = ch || ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(80);
    ImGui::DragFloat("duration (s)", &dur, 0.005f, 0.0f, 60.0f);
    ch = ch || ImGui::IsItemDeactivatedAfterEdit();
    if (ch) g.doc.setTransition(g.selTrans, exit, et, dur);
    ImGui::Text("Conditions");
    for (size_t c = 0; c < t.conditions.size(); ++c) {
      AnimCondDef cd = t.conditions[c];
      ImGui::PushID(static_cast<int>(c));
      bool cc = false;
      ImGui::SetNextItemWidth(110);
      if (ImGui::BeginCombo("##p", cd.param.c_str())) {
        for (const AnimParamDef& p : g.doc.params()) if (ImGui::Selectable(p.name.c_str(), p.name == cd.param)) { cd.param = p.name; cc = true; }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      static const char* modes[] = {"if", "ifNot", "greater", "less", "equals", "notEqual"};
      int mi = 0;
      for (int m = 0; m < 6; ++m) if (cd.mode == modes[m]) mi = m;
      ImGui::SetNextItemWidth(90);
      if (ImGui::Combo("##m", &mi, modes, 6)) { cd.mode = modes[mi]; cc = true; }
      if (cd.mode != "if" && cd.mode != "ifNot") {
        ImGui::SameLine();
        float v = static_cast<float>(cd.value);
        ImGui::SetNextItemWidth(70);
        ImGui::DragFloat("##v", &v, 0.05f);
        if (ImGui::IsItemDeactivatedAfterEdit()) { cd.value = v; cc = true; }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) { g.doc.removeCondition(g.selTrans, static_cast<int>(c)); ImGui::PopID(); break; }
      if (cc) g.doc.setCondition(g.selTrans, static_cast<int>(c), cd);
      ImGui::PopID();
    }
    if (!g.doc.params().empty() && ImGui::Button("Add condition")) {
      AnimCondDef c;
      c.param = g.doc.params()[0].name;
      const AnimParamType pt = g.doc.params()[0].type;
      c.mode = (pt == AnimParamType::Bool || pt == AnimParamType::Trigger) ? "if" : "greater";
      g.doc.addCondition(g.selTrans, c);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete transition")) { g.doc.removeTransition(g.selTrans); g.selTrans = -1; }
    return;
  }
  ImGui::TextDisabled("Select a state or a transition.");
  ImGui::SetNextItemWidth(140);
  ImGui::InputText("##ns", g.newState, sizeof g.newState);
  ImGui::SameLine();
  if (ImGui::Button("Add state")) {
    const int i = g.doc.addState(g.newState, nextSlotX(g.doc), nextSlotY(g.doc));
    if (i < 0) g.status = "state name is empty or taken";
    else { g.placed = false; g.selState = g.newState; }
  }
}

void drawAnimatorGraph(App& a) {
  GraphWin& g = G();
  if (!a.loaded || !g.show) return;
  if (!g.ctx) g.ctx = ImNodes::CreateContext();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(1100, 560), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 80, vp->WorkPos.y + 80), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Animator", &g.show)) { ImGui::End(); return; }
  if (!g.doc.loaded()) {
    ImGui::TextWrapped("No controller is open. Create an Animator Controller in the Project window (Create > Animator Controller) and double-click it, or pick one:");
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(a.project.root / "Assets", ec)) {
      const std::string n = e.path().filename().string();
      if (n.size() > 16 && n.compare(n.size() - 16, 16, ".controller.json") == 0 && ImGui::Selectable(fs::relative(e.path(), a.project.root).generic_string().c_str()))
        openController(a, fs::relative(e.path(), a.project.root).generic_string());
    }
    ImGui::End();
    return;
  }
  refreshLive(a);
  ImGui::Text("%s%s", g.rel.c_str(), g.doc.dirty() ? " *" : "");
  ImGui::SameLine();
  ImGui::BeginDisabled(!g.doc.canUndo());
  if (ImGui::Button("Undo")) { g.doc.undo(); g.placed = false; }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!g.doc.canRedo());
  if (ImGui::Button("Redo")) { g.doc.redo(); g.placed = false; }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!g.doc.dirty());
  if (ImGui::Button("Save")) {
    std::string err;
    const std::string bad = g.doc.validate();
    if (!bad.empty()) { g.status = bad; EditorLog::instance().warn("animator: " + g.rel + ": " + bad); }
    if (g.doc.save(err)) EditorLog::instance().info("animator: saved " + g.rel); else g.status = err;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (g.live) ImGui::TextColored(brandToken("ok"), "  live: %s%s", g.liveState.c_str(), g.liveNext.empty() ? "" : (" -> " + g.liveNext).c_str());
  else ImGui::TextDisabled("  (press Play in editor to see the running Animator)");
  const std::string bad = g.doc.validate();
  if (!bad.empty()) { ImGui::SameLine(); ImGui::TextColored(brandToken("danger"), "  [x] %s", bad.c_str()); }
  ImGui::BeginChild("##left", ImVec2(300, 0), ImGuiChildFlags_Borders);
  drawParams(g);
  ImGui::Dummy(ImVec2(0, 10));
  drawInspector(a, g);
  if (!g.status.empty()) ImGui::TextColored(brandToken("warn"), "%s", g.status.c_str());
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##graph", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  drawGraph(a, g);
  ImGui::EndChild();
  ImGui::End();
}

// ---- --ui-event verbs ---------------------------------------------------------------------------------------------------------------

std::vector<std::string> splitBar(const std::string& s) {
  std::vector<std::string> out;
  size_t p = 0;
  while (true) {
    const size_t q = s.find('|', p);
    out.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
    if (q == std::string::npos) break;
    p = q + 1;
  }
  return out;
}

bool graphHook(App& a, const std::string& act, const std::string& arg) {
  GraphWin& g = G();
  auto say = [&](const std::string& s) { std::printf("animator: %s\n", s.c_str()); std::fflush(stdout); };
  if (act == "animatorgraph") { g.show = arg != "off"; say(std::string("window ") + (g.show ? "open" : "closed")); return true; }
  if (act == "animctl") {
    const bool ok = openController(a, arg);
    say(ok ? "controller " + arg + " states=" + std::to_string(g.doc.states().size()) + " params=" + std::to_string(g.doc.params().size()) +
                 " transitions=" + std::to_string(g.doc.transitions().size())
           : "cannot open " + arg + ": " + g.status);
    return true;
  }
  if (act == "animlive") {
    refreshLive(a);
    say(g.live ? "live state=" + g.liveState + " next=" + (g.liveNext.empty() ? "-" : g.liveNext) + " t=" + fmtNum(g.liveTime) + " blend=" + fmtNum(g.liveBlend) : "live none");
    return true;
  }
  if (!g.doc.loaded()) {
    if (act.compare(0, 4, "anim") == 0) say("no controller is open (animctl:<path>)");
    return act.compare(0, 4, "anim") == 0;
  }
  const auto p = splitBar(arg);
  if (act == "animstate") {
    const std::string op = p[0];
    bool ok = false;
    if (op == "add" && p.size() > 1) { ok = g.doc.addState(p[1], nextSlotX(g.doc), nextSlotY(g.doc)) >= 0; g.placed = false; }
    else if (op == "rm" && p.size() > 1) { ok = g.doc.removeState(p[1]); g.placed = false; }
    else if (op == "rename" && p.size() > 2) ok = g.doc.renameState(p[1], p[2]);
    else if (op == "default" && p.size() > 1) ok = g.doc.setDefaultState(p[1]);
    else if (op == "motion" && p.size() > 2) {
      const std::string guid = looksLikeGuid(p[2]) ? p[2] : guidOfAsset(a, p[2]);
      ok = !guid.empty() && g.doc.setMotion(p[1], guid);
    } else if (op == "opts" && p.size() > 1 && g.doc.findState(p[1]) >= 0) {
      AnimStateDef s = g.doc.states()[g.doc.findState(p[1])];
      for (size_t i = 2; i < p.size(); ++i) {
        const size_t e = p[i].find('=');
        const std::string k = p[i].substr(0, e), v = e == std::string::npos ? "" : p[i].substr(e + 1);
        if (k == "loop") s.loop = v == "1" || v == "true";
        else if (k == "speed") s.speed = std::atof(v.c_str());
        else if (k == "speedparam") s.speedParameter = v;
      }
      ok = g.doc.setStateOptions(p[1], s.loop, s.speed, s.speedParameter);
    }
    say(std::string("state ") + op + " " + (p.size() > 1 ? p[1] : "") + (ok ? " ok" : " failed"));
    return true;
  }
  if (act == "animparam") {
    bool ok = false;
    if (p[0] == "add" && p.size() > 2) {
      const AnimParamType t = p[2] == "int" ? AnimParamType::Int : (p[2] == "bool" ? AnimParamType::Bool : (p[2] == "trigger" ? AnimParamType::Trigger : AnimParamType::Float));
      ok = g.doc.addParam(p[1], t, p.size() > 3 ? std::atof(p[3].c_str()) : 0) >= 0;
    } else if (p[0] == "rm" && p.size() > 1) ok = g.doc.removeParam(p[1]);
    say(std::string("param ") + p[0] + " " + (p.size() > 1 ? p[1] : "") + (ok ? " ok" : " failed"));
    return true;
  }
  if (act == "animtrans") {
    if (p.size() < 2) { say("usage: animtrans:<from or *>|<to>[|<cond>[;<cond>..]]"); return true; }
    const int t = g.doc.addTransition(p[0], p[1]);
    bool ok = t >= 0;
    if (ok && p.size() > 2) {
      size_t pos = 0;
      const std::string& cs = p[2];
      while (pos <= cs.size() && !cs.empty()) {
        const size_t q = cs.find(';', pos);
        AnimCondDef c;
        if (!parseCond(cs.substr(pos, q == std::string::npos ? std::string::npos : q - pos), c) || !g.doc.addCondition(t, c)) ok = false;
        if (q == std::string::npos) break;
        pos = q + 1;
      }
    }
    if (ok) { g.selTrans = t; g.selState.clear(); }
    say(std::string("transition ") + p[0] + " -> " + p[1] + (ok ? " ok index=" + std::to_string(t) : " failed"));
    return true;
  }
  if (act == "animtransset" && p.size() > 1) {
    const int idx = std::atoi(p[0].c_str());
    if (idx < 0 || idx >= static_cast<int>(g.doc.transitions().size())) { say("no transition " + p[0]); return true; }
    AnimTransDef t = g.doc.transitions()[idx];
    for (size_t i = 1; i < p.size(); ++i) {
      const size_t e = p[i].find('=');
      const std::string k = p[i].substr(0, e), v = e == std::string::npos ? "" : p[i].substr(e + 1);
      if (k == "exit") { t.hasExitTime = true; t.exitTime = std::atof(v.c_str()); }
      else if (k == "noexit") t.hasExitTime = false;
      else if (k == "duration") t.duration = std::atof(v.c_str());
    }
    say(std::string("transition ") + p[0] + (g.doc.setTransition(idx, t.hasExitTime, t.exitTime, t.duration) ? " ok" : " failed"));
    return true;
  }
  if (act == "animtransrm") { say(std::string("transition rm ") + arg + (g.doc.removeTransition(std::atoi(arg.c_str())) ? " ok" : " failed")); return true; }
  if (act == "animctlsave") {
    std::string err;
    const std::string bad = g.doc.validate();
    say(bad.empty() ? (g.doc.save(err) ? "saved " + g.rel : "save failed: " + err) : "invalid: " + bad);
    if (bad.empty()) return true;
    g.doc.save(err);  // an invalid controller is still saved (the cooker will say why it refuses it)
    return true;
  }
  if (act == "animctlundo") { g.doc.undo(); g.placed = false; say("undo"); return true; }
  if (act == "animctlredo") { g.doc.redo(); g.placed = false; say("redo"); return true; }
  if (act == "animctlsel") {
    if (p[0] == "state" && p.size() > 1) { g.selState = p[1]; g.selTrans = -1; }
    else if (p[0] == "trans" && p.size() > 1) { g.selTrans = std::atoi(p[1].c_str()); g.selState.clear(); }
    return true;
  }
  if (act == "animctldump") {
    const std::string bad = g.doc.validate();
    say("controller=" + g.rel + " states=" + std::to_string(g.doc.states().size()) + " params=" + std::to_string(g.doc.params().size()) + " transitions=" +
        std::to_string(g.doc.transitions().size()) + " default=" + g.doc.defaultState() + " dirty=" + (g.doc.dirty() ? "1" : "0") + " valid=" + (bad.empty() ? "ok" : bad));
    for (const AnimStateDef& s : g.doc.states())
      say("  state " + s.name + " motion=" + (s.motion.empty() ? "-" : s.motion) + " loop=" + (s.loop ? "1" : "0") + " speed=" + fmtNum(s.speed) +
          (s.speedParameter.empty() ? "" : " speedParam=" + s.speedParameter));
    for (const AnimParamDef& q : g.doc.params()) {
      static const char* tn[] = {"float", "int", "bool", "trigger"};
      say(std::string("  param ") + q.name + " " + tn[static_cast<int>(q.type)] + " default=" + fmtNum(q.defaultValue));
    }
    for (size_t i = 0; i < g.doc.transitions().size(); ++i) {
      const AnimTransDef& t = g.doc.transitions()[i];
      std::string cs;
      for (const AnimCondDef& c : t.conditions) cs += (cs.empty() ? "" : "; ") + condText(c);
      say("  trans " + std::to_string(i) + " " + t.from + " -> " + t.to + " exit=" + (t.hasExitTime ? fmtNum(t.exitTime) : "-") + " duration=" + fmtNum(t.duration) + " [" + cs + "]");
    }
    return true;
  }
  return false;
}

}  // namespace

RENSGA_EDITOR_PANEL(animator_graph, "Animator", drawAnimatorGraph, graphHook, nullptr, 165, 126, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
