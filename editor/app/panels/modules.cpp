// Build support modules UI. Colors are brand tokens only; every status color is paired with an icon and a word.
#include "modules.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "panel_registry.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

ModulesUi& modulesUi() {
  static ModulesUi* ui = new ModulesUi();  // leaked on purpose: the model joins its thread in the destructor
  return *ui;
}

ModulesModel& modulesModel(App& a) {
  ModulesUi& ui = modulesUi();
  if (!ui.model) ui.model = std::make_unique<ModulesModel>();
  if (!ui.settingsLoaded) {
    ui.settingsLoaded = true;
    for (const auto& kv : a.hub.settings.modulePaths) ui.model->setLocated(kv.first, kv.second);
  }
  return *ui.model;
}

static void say(ModulesUi& ui, int kind, const std::string& text) {
  ui.notice = text;
  ui.noticeKind = kind;
  EditorLog& log = EditorLog::instance();
  if (kind >= 3) log.error("modules: " + text);
  else if (kind == 2) log.warn("modules: " + text);
  else log.info("modules: " + text);
}

// ---------------------------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------------------------
static const char* statusToken(ModuleStatus s) {
  switch (s) {
    case ModuleStatus::Installed: case ModuleStatus::Located: return "ok";
    case ModuleStatus::NotInstalled: return "text-2";
    case ModuleStatus::Unpublished: return "warn";
    case ModuleStatus::Manual: return "info";
    case ModuleStatus::NoHost: return "muted";
  }
  return "muted";
}

// kind: 0 info, 1 ok, 2 warn, 3 error. Drawn with the draw list so no glyph is needed from the font.
static void kindIcon(ImDrawList* dl, ImVec2 c, float r, int kind, ImU32 col) {
  const float t = 1.6f;
  if (kind == 1) {
    dl->AddCircleFilled(c, r, col, 20);
    const ImU32 bg = ImGui::GetColorU32(brandToken("bg"));
    dl->AddPolyline(std::vector<ImVec2>{ImVec2(c.x - r * 0.5f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.4f), ImVec2(c.x + r * 0.55f, c.y - r * 0.4f)}.data(), 3, bg, 0, t);
  } else if (kind == 2) {
    dl->AddTriangle(ImVec2(c.x, c.y - r), ImVec2(c.x - r, c.y + r * 0.8f), ImVec2(c.x + r, c.y + r * 0.8f), col, t);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.3f), ImVec2(c.x, c.y + r * 0.25f), col, t);
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.55f), 1.0f, col);
  } else if (kind == 3) {
    dl->AddCircle(c, r, col, 20, t);
    dl->AddLine(ImVec2(c.x - r * 0.4f, c.y - r * 0.4f), ImVec2(c.x + r * 0.4f, c.y + r * 0.4f), col, t);
    dl->AddLine(ImVec2(c.x - r * 0.4f, c.y + r * 0.4f), ImVec2(c.x + r * 0.4f, c.y - r * 0.4f), col, t);
  } else {
    dl->AddCircle(c, r, col, 20, t);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.1f), ImVec2(c.x, c.y + r * 0.5f), col, t);
    dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.5f), 1.0f, col);
  }
}

static void statusIcon(ImDrawList* dl, ImVec2 c, float r, ModuleStatus s) {
  const ImU32 col = ImGui::GetColorU32(brandToken(statusToken(s)));
  const float t = 1.6f;
  switch (s) {
    case ModuleStatus::Installed: kindIcon(dl, c, r, 1, col); break;
    case ModuleStatus::NotInstalled: dl->AddCircle(c, r, col, 20, t); break;       // empty circle
    case ModuleStatus::Unpublished: kindIcon(dl, c, r, 2, col); break;             // triangle
    case ModuleStatus::Manual:                                                      // diamond
      dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col, t);
      break;
    case ModuleStatus::Located:
      dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);
      break;
    case ModuleStatus::NoHost: kindIcon(dl, c, r, 3, col); break;
  }
}

static void statusCell(const ModuleStatus s) {
  const float h = ImGui::GetTextLineHeight(), r = h * 0.36f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  statusIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + r + 1, p.y + h * 0.5f), r, s);
  ImGui::Dummy(ImVec2(r * 2 + 8, h));
  ImGui::SameLine(0, 0);
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken(statusToken(s)));
  ImGui::TextUnformatted(moduleStatusWord(s));
  ImGui::PopStyleColor();
}

static void noticeLine(const std::string& text, int kind) {
  if (text.empty()) return;
  static const char* tok[] = {"info", "ok", "warn", "danger"};
  const char* t = tok[std::clamp(kind, 0, 3)];
  const float h = ImGui::GetTextLineHeight(), r = h * 0.4f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  kindIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + r + 1, p.y + h * 0.5f), r, kind, ImGui::GetColorU32(brandToken(t)));
  ImGui::Dummy(ImVec2(r * 2 + 8, h));
  ImGui::SameLine(0, 0);
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken(t));
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

static bool primaryBtn(const char* label, ImVec2 size = ImVec2(0, 0)) {
  ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary"));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brandToken("primary-hover"));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, brandToken("primary-hover"));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
  bool r = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return r;
}

static std::string jobText(const ModulesModel& m, const JobSnapshot& j) {
  const ModuleRow* r = m.find(j.moduleId);
  std::string name = r ? r->name : j.moduleId;
  if (j.phase == "download" && j.total) return "Downloading " + name + ": " + formatBytes(j.done) + " of " + formatBytes(j.total);
  if (j.phase == "download") return "Downloading " + name + ": " + formatBytes(j.done);
  if (j.phase == "verify") return "Checking sha256 of " + name;
  if (j.phase == "extract") return "Unpacking " + name;
  if (j.phase == "check") return "Testing " + name;
  if (j.phase == "postinstall") return "Finishing " + name + (j.detail.empty() ? "" : " (" + j.detail + ")");
  return "Preparing " + name;
}

static void jobBar(const JobSnapshot& j, float width) {
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, brandRamp("cerrado.500"));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, brandToken("border"));
  if (j.total) ImGui::ProgressBar(j.fraction(), ImVec2(width, 12), "");
  else {
    const float t = static_cast<float>(ImGui::GetTime());
    ImGui::ProgressBar(-t, ImVec2(width, 12), "");   // negative = indeterminate sweep
  }
  ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------------------------
// Modules list (shared by the Hub tab and the window)
// ---------------------------------------------------------------------------------------------
static void startInstallUi(App& a, const std::string& id) {
  ModulesUi& ui = modulesUi();
  ModulesModel& m = modulesModel(a);
  std::string why;
  if (!m.startInstall(id, why)) say(ui, 2, why);
  else { ui.notice.clear(); ui.selected = id; }
}

static void drawJobBanner(App& a, ModulesModel& m, ModulesUi& ui) {
  const JobSnapshot j = m.job();
  if (j.state == JobState::Running) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(jobText(m, j).c_str());
    ImGui::SameLine();
    jobBar(j, std::max(120.0f, ImGui::GetContentRegionAvail().x - 100));
    ImGui::SameLine();
    if (ImGui::Button("Cancel##job", ImVec2(84, 0))) m.cancel();
  } else if (!ui.notice.empty()) {
    noticeLine(ui.notice, ui.noticeKind);
    ImGui::SameLine();
    if (ImGui::SmallButton("Dismiss")) ui.notice.clear();
  }
  (void)a;
}

static void drawDetails(App& a, ModulesModel& m, ModulesUi& ui, float height) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, brandToken("surface"));
  ImGui::BeginChild("##moddetails", ImVec2(0, height), ImGuiChildFlags_Borders);
  const ModuleRow* r = m.find(ui.selected);
  if (!r) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
    ImGui::TextUnformatted("Select a module to see its license, source and install location.");
    ImGui::PopStyleColor();
  } else {
    ImGui::PushFont(a.fonts.uiBold, 16);
    ImGui::Text("%s", r->name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
    ImGui::Text("%s  version %s", r->id.c_str(), r->version.c_str());
    ImGui::PopStyleColor();
    auto field = [&](const char* k, const std::string& v) {
      if (v.empty()) return;
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("text-2"));
      ImGui::Text("%s", k);
      ImGui::PopStyleColor();
      ImGui::SameLine(110);
      ImGui::PushTextWrapPos(0);
      ImGui::TextUnformatted(v.c_str());
      ImGui::PopTextWrapPos();
    };
    std::string req;
    for (const std::string& d : r->requires_) req += (req.empty() ? "" : ", ") + d;
    field("Location", r->location.empty() ? std::string("not installed") : r->location);
    field("Download", r->size ? formatBytes(r->totalSize) + (r->totalSize != r->size ? " with dependencies" : "") : std::string());
    field("Requires", req);
    field("License", r->licenseText);
    field("Source", r->source);
    field("Notice", r->notice);
    field("Note", r->note);
    if (!r->licenseIds.empty() && r->status != ModuleStatus::Installed) {
      ImGui::Spacing();
      for (const std::string& l : r->licenseIds) {
        bool ok = m.accepted(l);
        std::string label = "I accept the license: " + l + "##acc_" + l;
        if (ImGui::Checkbox(label.c_str(), &ok)) m.accept(l, ok);
      }
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
      ImGui::TextWrapped("The files come from their owner and are downloaded from there only after you accept. Rensga does not redistribute them.");
      ImGui::PopStyleColor();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

static void openLocate(ModulesUi& ui, const ModuleRow& r) {
  ui.openLocate = true;
  ui.locateModule = r.id;
  std::snprintf(ui.locatePath, sizeof ui.locatePath, "%s", r.location.c_str());
  ui.locateError.clear();
}

static void drawModulesList(App& a, float detailsHeight) {
  ModulesUi& ui = modulesUi();
  ModulesModel& m = modulesModel(a);
  ImGui::PushFont(a.fonts.uiBold, 17);
  ImGui::PushStyleColor(ImGuiCol_Text, brandRamp("cerrado.300"));
  ImGui::TextUnformatted("Build support modules");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
  ImGui::Text("  for %s, installed per user in %s", m.manager().host().c_str(), m.manager().modulesDir().string().c_str());
  ImGui::PopStyleColor();
  for (const std::string& e : m.catalogErrors()) noticeLine("catalog: " + e, 3);
  ImGui::Spacing();
  drawJobBanner(a, m, ui);
  ImGui::Spacing();

  const JobSnapshot job = m.job();
  const bool busy = m.busy();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, brandToken("surface"));
  ImGui::BeginChild("##modlist", ImVec2(0, -detailsHeight - 8), ImGuiChildFlags_Borders);
  if (m.rows().empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
    ImGui::TextUnformatted("No modules are cataloged. The catalog is tools/modules/manifest.d (RENSGA_MODULE_CATALOG overrides it).");
    ImGui::PopStyleColor();
  } else if (ImGui::BeginTable("##mods", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 170);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableHeadersRow();
    std::string removeId, verifyId;
    for (size_t i = 0; i < m.rows().size(); ++i) {
      const ModuleRow& r = m.rows()[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow(ImGuiTableRowFlags_None, 40);
      ImGui::TableSetColumnIndex(0);
      ImGui::PushFont(a.fonts.uiBold, 15);
      if (ImGui::Selectable(r.name.c_str(), ui.selected == r.id, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 34))) ui.selected = r.id;
      ImGui::PopFont();
      ImGui::SameLine(0, 10);
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
      ImGui::TextUnformatted(r.id.c_str());
      ImGui::PopStyleColor();
      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(r.version.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(r.size ? formatBytes(r.size).c_str() : "-");
      ImGui::TableSetColumnIndex(3);
      ImGui::AlignTextToFramePadding();
      statusCell(r.status);
      if (!r.note.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.note.c_str());
      ImGui::TableSetColumnIndex(4);
      if (busy && job.moduleId == r.id) {
        jobBar(job, 110);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(70, 0))) m.cancel();
      } else {
        switch (r.status) {
          case ModuleStatus::NotInstalled: {
            const bool needLicense = !m.unacceptedLicenses(r.id).empty();
            ImGui::BeginDisabled(busy || needLicense);
            if (ImGui::Button("Install", ImVec2(80, 0))) startInstallUi(a, r.id);
            ImGui::EndDisabled();
            if (needLicense && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Accept the license in the details below first.");
            if (needLicense) { ImGui::SameLine(); ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn")); ImGui::TextUnformatted("License"); ImGui::PopStyleColor(); }
            break;
          }
          case ModuleStatus::Installed:
            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Verify", ImVec2(70, 0))) verifyId = r.id;
            ImGui::SameLine();
            if (ImGui::Button("Remove", ImVec2(70, 0))) removeId = r.id;
            ImGui::EndDisabled();
            break;
          case ModuleStatus::Manual: case ModuleStatus::Located:
            if (ImGui::Button("Locate...", ImVec2(90, 0))) openLocate(ui, r);
            if (r.status == ModuleStatus::Located) { ImGui::SameLine(); if (ImGui::Button("Clear", ImVec2(60, 0))) { a.hub.settings.modulePaths.erase(r.id); a.hub.settings.save(); m.setLocated(r.id, ""); } }
            break;
          default: break;
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
    if (!removeId.empty()) {
      rensga::modules::Result res = m.remove(removeId);
      say(ui, res.ok ? 1 : 3, res.message);
    }
    if (!verifyId.empty()) {
      rensga::modules::Result res = m.verify(verifyId);
      say(ui, res.ok ? 1 : 3, res.message);
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  drawDetails(a, m, ui, detailsHeight);
}

// ---------------------------------------------------------------------------------------------
// Hub tab strip and the floating window
// ---------------------------------------------------------------------------------------------
bool drawHubTabs(App& a, float footerHeight) {
  ModulesUi& ui = modulesUi();
  const char* names[2] = {"Projects", "Modules"};
  for (int i = 0; i < 2; ++i) {
    const bool on = (i == 1) == ui.hubTab;
    if (i) ImGui::SameLine(0, 4);
    ImGui::PushStyleColor(ImGuiCol_Button, on ? brandToken("surface-2") : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? brandToken("text") : brandToken("text-2"));
    if (ImGui::Button(names[i], ImVec2(110, 30))) ui.hubTab = i == 1;
    ImGui::PopStyleColor(2);
    if (on) {
      ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x + 6, mx.y - 2), ImVec2(mx.x - 6, mx.y), ImGui::GetColorU32(brandToken("primary")));
    }
  }
  ImGui::Spacing();
  if (!ui.hubTab) return false;
  (void)footerHeight;
  modulesModel(a);
  ImGui::BeginChild("##hubmodules", ImVec2(0, -footerHeight), ImGuiChildFlags_None);
  drawModulesList(a, 210);
  ImGui::EndChild();
  return true;
}

void openModulesFromHub(App& a) {
  ModulesUi& ui = modulesUi();
  modulesModel(a);
  if (a.hub.mode == Mode::Picker) ui.hubTab = true;
  else ui.showWindow = true;
}

void drawModulesWindow(App& a) {
  ModulesUi& ui = modulesUi();
  if (!ui.showWindow) return;
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(980.0f, vp->Size.x - 80), std::min(620.0f, vp->Size.y - 80)), ImGuiCond_Appearing);
  if (ImGui::Begin("Build Support Modules", &ui.showWindow, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) drawModulesList(a, 190);
  ImGui::End();
}

// ---------------------------------------------------------------------------------------------
// Missing-module gate and dialog
// ---------------------------------------------------------------------------------------------
static void continueAfter(App& a, AfterModule what) {
  if (what == AfterModule::PlayConsole) {
    EditorLog::instance().info("modules: module installed, continuing with Play on console");
    doPlayConsole(a);
  }
}

bool gateTargetModule(App& a, const std::string& target, AfterModule after) {
  ModulesUi& ui = modulesUi();
  ModulesModel& m = modulesModel(a);
  if (ui.noPrompt || !m.missingForTarget(target)) return true;
  const ModuleRow* r = m.forTarget(target);
  if (!r) return true;
  ui.gateModule = r->id;
  ui.gateTarget = target;
  ui.gateAfter = after;
  ui.gateStarted = false;
  ui.openGate = true;
  return false;
}

std::string missingModuleLine(App& a, const std::string& target) {
  ModulesModel& m = modulesModel(a);
  if (!m.missingForTarget(target)) return "";
  const ModuleRow* r = m.forTarget(target);
  if (!r) return "";
  if (r->status == ModuleStatus::Unpublished) return r->name + " is not published for this computer yet. You can still create the project and use Play in editor.";
  if (r->status == ModuleStatus::NotInstalled) return r->name + " is not installed (" + formatBytes(r->totalSize) + "). Install it from Hub > Modules before you build for this target.";
  return r->name + " is not installed. See Hub > Modules.";
}

void drawModuleDialogs(App& a) {
  ModulesUi& ui = modulesUi();
  if (!ui.model) return;
  ModulesModel& m = *ui.model;
  ImVec2 c = ImGui::GetMainViewport()->GetCenter();

  if (ui.openGate) { ImGui::OpenPopup("Build support missing"); ui.openGate = false; }
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0));
  if (ImGui::BeginPopupModal("Build support missing", nullptr, ImGuiWindowFlags_NoResize)) {
    const ModuleRow* r = m.find(ui.gateModule);
    const JobSnapshot j = m.job();
    const bool mine = ui.gateStarted && j.moduleId == ui.gateModule;
    if (!r) {
      ImGui::TextUnformatted("This module is no longer in the catalog.");
    } else {
      const bool installed = r->status == ModuleStatus::Installed && !m.missingForTarget(ui.gateTarget);
      ImGui::PushFont(a.fonts.uiBold, 16);
      if (r->status == ModuleStatus::NotInstalled || (mine && j.state == JobState::Running))
        ImGui::TextWrapped("%s is not installed (%s). Install now?", r->name.c_str(), formatBytes(r->totalSize ? r->totalSize : r->size).c_str());
      else if (installed) ImGui::TextWrapped("%s is installed.", r->name.c_str());
      else ImGui::TextWrapped("%s cannot be installed here.", r->name.c_str());
      ImGui::PopFont();
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("text-2"));
      ImGui::TextWrapped("The %s target needs it to build and run. The download is checked with sha256 and installed for you only, in %s. No Docker is needed.",
                         ui.gateTarget.c_str(), m.manager().modulesDir().string().c_str());
      ImGui::PopStyleColor();
      if (!r->note.empty()) { ImGui::Spacing(); noticeLine(r->note, 2); }
      for (const std::string& l : r->licenseIds) {
        if (r->status == ModuleStatus::Installed) break;
        bool ok = m.accepted(l);
        std::string label = "I accept the license: " + l + "##gacc_" + l;
        ImGui::Spacing();
        if (ImGui::Checkbox(label.c_str(), &ok)) m.accept(l, ok);
      }
      ImGui::Spacing();
      if (mine && j.state == JobState::Running) {
        ImGui::TextUnformatted(jobText(m, j).c_str());
        jobBar(j, -1);
      } else if (mine && (j.state == JobState::Failed || j.state == JobState::Cancelled)) {
        noticeLine(j.message, j.state == JobState::Failed ? 3 : 2);
      }
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      const bool running = mine && j.state == JobState::Running;
      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 10 - 140 - 8 - 110);
      if (ImGui::Button(running ? "Cancel install" : "Cancel", ImVec2(running ? 140 : 110, 32))) {
        if (running) m.cancel();
        else { ImGui::CloseCurrentPopup(); EditorLog::instance().info("modules: dialog cancelled, nothing was installed"); }
      }
      ImGui::SameLine();
      const bool can = r->status == ModuleStatus::NotInstalled && !running && !m.busy() && m.unacceptedLicenses(r->id).empty();
      ImGui::BeginDisabled(!can);
      if (primaryBtn("Install now", ImVec2(140, 32))) {
        std::string why;
        if (m.startInstall(r->id, why)) ui.gateStarted = true; else say(ui, 3, why);
      }
      ImGui::EndDisabled();
      if (installed && mine && j.state == JobState::Done) {}  // closed by tickModules
    }
    if (ui.gateModule.empty()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ui.openLocate) { ImGui::OpenPopup("Locate module"); ui.openLocate = false; }
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0));
  if (ImGui::BeginPopupModal("Locate module", nullptr, ImGuiWindowFlags_NoResize)) {
    const ModuleRow* r = m.find(ui.locateModule);
    ImGui::TextWrapped("Folder where %s is installed. %s", r ? r->name.c_str() : ui.locateModule.c_str(), r ? r->note.c_str() : "");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("##locpath", ui.locatePath, sizeof ui.locatePath, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!ui.locateError.empty()) noticeLine(ui.locateError, 3);
    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(96, 0))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (primaryBtn("Use this folder", ImVec2(140, 0)) || enter) {
      std::error_code ec;
      if (!ui.locatePath[0] || !fs::is_directory(fs::path(ui.locatePath), ec)) ui.locateError = "That folder does not exist.";
      else {
        a.hub.settings.modulePaths[ui.locateModule] = ui.locatePath;
        a.hub.settings.save();
        m.setLocated(ui.locateModule, ui.locatePath);
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
}

void tickModules(App& a) {
  ModulesUi& ui = modulesUi();
  if (!ui.model) return;
  if (!ui.model->pump()) return;
  const JobSnapshot j = ui.model->job();
  const int kind = j.state == JobState::Done ? 1 : j.state == JobState::Cancelled ? 2 : 3;
  say(ui, kind, j.message);
  if (ui.gateStarted && j.moduleId == ui.gateModule) {
    if (j.state == JobState::Done) {
      const AfterModule what = ui.gateAfter;
      ui.gateModule.clear();   // the dialog closes itself
      ui.gateStarted = false;
      continueAfter(a, what);
    }
  }
}

// ---------------------------------------------------------------------------------------------
// --ui-event verbs
// ---------------------------------------------------------------------------------------------
bool modulesHook(App& a, const std::string& act, const std::string& arg) {
  ModulesUi& ui = modulesUi();
  if (act == "modules") { openModulesFromHub(a); return true; }
  if (act == "modhub") { modulesModel(a); ui.hubTab = arg != "off"; return true; }
  if (act.rfind("mod", 0) != 0 && act != "playconsole") return false;
  ModulesModel& m = modulesModel(a);
  if (act == "modselect") { ui.selected = arg; return true; }
  if (act == "modinstall") {
    std::string why;
    ui.selected = arg;
    if (m.startInstall(arg, why)) std::printf("modinstall: %s started\n", arg.c_str());
    else { std::printf("modinstall: %s refused: %s\n", arg.c_str(), why.c_str()); say(ui, 2, why); }
    return true;
  }
  if (act == "modaccept") {   // a module id (its license ids and its dependencies') or a license id
    if (const ModuleRow* r = m.find(arg)) {
      for (const std::string& l : r->licenseIds) m.accept(l, true);
      for (const std::string& d : r->requires_) if (const ModuleRow* dr = m.find(d)) for (const std::string& l : dr->licenseIds) m.accept(l, true);
    } else m.accept(arg, true);
    std::printf("modaccept: %s\n", arg.c_str());
    return true;
  }
  if (act == "modremove") {
    rensga::modules::Result r = m.remove(arg);
    say(ui, r.ok ? 1 : 3, r.message);
    std::printf("modremove: %s %s\n", arg.c_str(), r.ok ? "ok" : ("failed: " + r.message).c_str());
    return true;
  }
  if (act == "modverify") {
    rensga::modules::Result r = m.verify(arg);
    say(ui, r.ok ? 1 : 3, r.message);
    std::printf("modverify: %s %s\n", arg.c_str(), r.ok ? "ok" : ("failed: " + r.message).c_str());
    return true;
  }
  if (act == "modlocate") {   // modlocate:<id>|<folder>
    size_t bar = arg.find('|');
    if (bar == std::string::npos) return true;
    const std::string id = arg.substr(0, bar), dir = arg.substr(bar + 1);
    a.hub.settings.modulePaths[id] = dir;
    a.hub.settings.save();
    m.setLocated(id, dir);
    std::printf("modlocate: %s %s\n", id.c_str(), dir.c_str());
    return true;
  }
  if (act == "modstate") {    // one line per module (or just <id>), for the tests
    for (const ModuleRow& r : m.rows()) {
      if (!arg.empty() && r.id != arg) continue;
      std::printf("modstate: id=%s status=%s size=%llu licenses=%zu root=%s\n", r.id.c_str(), moduleStatusWord(r.status),
                  static_cast<unsigned long long>(r.size), r.licenseIds.size(), r.location.c_str());
    }
    const JobSnapshot j = m.job();
    std::printf("modjob: state=%d module=%s phase=%s message=%s\n", static_cast<int>(j.state), j.moduleId.c_str(), j.phase.c_str(), j.message.c_str());
    std::fflush(stdout);
    return true;
  }
  if (act == "playconsole") {   // Play on console, whatever the Play dropdown says
    doPlayConsole(a);
    std::printf("playconsole: gate=%s\n", ui.openGate ? "dialog" : "open");
    return true;
  }
  if (act == "modgate") {       // modgate:<target>: the dialog without a follow-up action
    const bool go = gateTargetModule(a, arg, AfterModule::None);
    std::printf("modgate: %s %s\n", arg.c_str(), go ? "proceed" : "dialog");
    return true;
  }
  if (act == "moddialoginstall") {
    std::string why;
    if (m.startInstall(ui.gateModule, why)) ui.gateStarted = true;
    else say(ui, 3, why);
    std::printf("moddialoginstall: %s\n", why.empty() ? "started" : why.c_str());
    return true;
  }
  if (act == "moddialogcancel") { ui.gateModule.clear(); ui.gateStarted = false; std::printf("moddialogcancel\n"); return true; }
  return false;
}

bool modulesUiHolds(App&, const std::string& e) {
  static std::string waiting;
  static std::chrono::steady_clock::time_point since;
  if (e.rfind("modwait", 0) != 0) { waiting.clear(); return false; }
  ModulesUi& ui = modulesUi();
  if (!ui.model) return false;
  const auto now = std::chrono::steady_clock::now();
  if (waiting != e) { waiting = e; since = now; }
  if (now - since > std::chrono::seconds(120)) { EditorLog::instance().error("ui-event " + e + " timed out"); waiting.clear(); return false; }
  if (ui.model->busy() || ui.model->finishedPending()) return true;   // tickModules joins the job and runs the follow-up
  waiting.clear();
  return false;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(modules_window, "Build Support Modules", drawModulesWindow, modulesHook, modulesUiHolds, 20, 30, ::rensga::editor::PanelDraw::OverlayEditor)
RENSGA_EDITOR_PANEL(module_dialogs, "Module dialogs", drawModuleDialogs, nullptr, nullptr, 30, 0, ::rensga::editor::PanelDraw::Overlay)

}  // namespace editor
}  // namespace rensga
