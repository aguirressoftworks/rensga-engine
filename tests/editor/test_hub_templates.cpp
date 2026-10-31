// Hub template model (F4.09 / F4.10): the six template cards from editor/data/hub.json, which are selectable, and that
// createProject copies exactly the template folder (no Library/, no thumbnail.png) and refuses the greyed-out ones.
// No console names here: the ids come from hub.json.
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "rensga/editor/hub.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
fs::path fresh(const std::string& n) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / ("hub-" + n);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}

HubData realData() {
  HubData d;
  CHECK(d.load(fs::path(RENSGA_SOURCE_DIR) / "editor" / "data" / "hub.json", fs::path(RENSGA_SOURCE_DIR) / "templates"));
  return d;
}

HubSettings settingsIn(const fs::path& dir) {
  HubSettings s;
  s.load(dir);
  return s;
}

std::set<std::string> filesUnder(const fs::path& root) {
  std::set<std::string> out;
  for (fs::recursive_directory_iterator it(root), end; it != end; ++it)
    if (it->is_regular_file()) out.insert(fs::relative(it->path(), root).generic_string());
  return out;
}

json readJson(const fs::path& p) {
  std::ifstream f(p);
  return json::parse(f, nullptr, false);
}
}  // namespace

TEST(hub_six_template_cards) {
  HubData d = realData();
  CHECK(d.templates.size() == 6);
  std::set<std::string> avail, off;
  for (const HubTemplate& t : d.templates) (t.selectable() ? avail : off).insert(t.dir);
  CHECK((avail == std::set<std::string>{"psp-2d", "psp-3d", "ps2-2d"}));
  CHECK((off == std::set<std::string>{"psx-2d", "psx-3d", "ps2-3d"}));
  // every disabled card names the phase that delivers it
  for (const HubTemplate& t : d.templates) {
    CHECK(!t.selectable() == !t.comingIn.empty());
    CHECK(!t.description.empty());
  }
  const int psx2d = d.findTemplate("psx", "2d"), ps23d = d.findTemplate("ps2", "3d"), psx3d = d.findTemplate("psx", "3d");
  CHECK(psx2d >= 0 && d.templates[static_cast<size_t>(psx2d)].comingIn == "PSX Template 5");
  CHECK(psx3d >= 0 && d.templates[static_cast<size_t>(psx3d)].comingIn == "PSX Template 4");
  CHECK(ps23d >= 0 && d.templates[static_cast<size_t>(ps23d)].comingIn == "PS2 Template 4");
  CHECK(d.findTemplate("psp", "4d") < 0);
  // psx is not selectable, psp and ps2 are (ps2 through its 2D card)
  for (size_t i = 0; i < d.apis.size(); ++i) CHECK(d.apiSelectable(i) == (d.apis[i].id != "psx"));
  CHECK(d.templates[static_cast<size_t>(d.findTemplate("ps2", "2d"))].note.find("F8") != std::string::npos);
  // dump lines for the UI test
  const std::vector<std::string> lines = d.dumpTemplates();
  CHECK(lines.size() == 6);
  size_t nAvail = 0, nOff = 0;
  for (const std::string& l : lines) {
    nAvail += l.find("state=available") != std::string::npos;
    nOff += l.find("state=disabled reason=Coming in ") != std::string::npos;
  }
  CHECK(nAvail == 3 && nOff == 3);
}

TEST(hub_default_template_prefers_3d) {
  HubData d = realData();
  CHECK(d.defaultTemplate("psp") == d.findTemplate("psp", "3d"));
  CHECK(d.defaultTemplate("ps2") == d.findTemplate("ps2", "2d"));   // its 3D card is greyed out
  CHECK(d.defaultTemplate("psx") < 0);
  CHECK(d.defaultTemplate("nope") < 0);
}

TEST(hub_create_copies_the_chosen_folder) {
  HubData d = realData();
  const fs::path home = fresh("create");
  HubSettings s = settingsIn(home);
  for (const char* dir : {"psp-2d", "psp-3d", "ps2-2d"}) {
    const HubTemplate* t = nullptr;
    for (const HubTemplate& x : d.templates) if (x.dir == dir) t = &x;
    CHECK(t != nullptr);
    if (!t) continue;
    fs::path out;
    std::string err;
    const std::string name = std::string("My_") + dir;
    CHECK(createProject(d, s, name, *t, "console", out, err));
    CHECK(err.empty());
    CHECK(out == s.projectsPath / name);
    const fs::path src = fs::path(RENSGA_SOURCE_DIR) / "templates" / dir;
    std::set<std::string> want = filesUnder(src), got = filesUnder(out);
    for (auto it = want.begin(); it != want.end();) {
      if (it->rfind("Library/", 0) == 0 || *it == "thumbnail.png") it = want.erase(it); else ++it;
    }
    got.erase(".gitignore");
    for (auto it = got.begin(); it != got.end();) { if (it->rfind("Library/", 0) == 0) it = got.erase(it); else ++it; }
    CHECK(got == want);
    CHECK(!fs::exists(out / "thumbnail.png"));
    CHECK(fs::is_directory(out / "Library"));
    CHECK(fs::is_empty(out / "Library"));   // created empty, nothing copied from the template's own Library/
    json pj = readJson(out / "ProjectSettings" / "Project.json");
    CHECK(pj.value("name", "") == name);
    CHECK(pj.value("target", "") == t->api);
    CHECK(pj.value("buildTarget", "") == "console");
    // the ConsoleTarget is the template's own: 2D templates carry their profile's resolution
    json ct = readJson(out / "ProjectSettings" / "ConsoleTarget.json");
    CHECK(ct.value("profile", "") == t->api);
    if (t->kind == "2d") {
      CHECK(ct["resolution"][0].get<int>() == (t->api == "psp" ? 480 : 640));
      CHECK(ct["resolution"][1].get<int>() == (t->api == "psp" ? 272 : 448));
    }
  }
}

TEST(hub_disabled_cards_refuse_create) {
  HubData d = realData();
  const fs::path home = fresh("refuse");
  HubSettings s = settingsIn(home);
  for (const HubTemplate& t : d.templates) {
    if (t.selectable()) continue;
    fs::path out;
    std::string err;
    CHECK(!createProject(d, s, "Nope_" + t.dir, t, "console", out, err));
    CHECK(err.find("Coming in " + t.comingIn) != std::string::npos);
    CHECK(!fs::exists(s.projectsPath / ("Nope_" + t.dir)));   // nothing on disk
  }
  // the API overload has no template for psx and says so
  fs::path out;
  std::string err;
  CHECK(!createProject(d, s, "PsxGame", "psx", "console", out, err));
  CHECK(!err.empty());
  CHECK(!fs::exists(s.projectsPath / "PsxGame"));
  // a bad name is refused before anything else
  CHECK(!createProject(d, s, "bad/name", d.templates[static_cast<size_t>(d.findTemplate("psp", "2d"))], "console", out, err));
}

TEST(hub_missing_folder_is_not_selectable) {
  // hub.json says available, the folder is not there: the card stays greyed out and create refuses.
  const fs::path home = fresh("missing");
  const fs::path tdir = home / "templates";
  fs::create_directories(tdir / "a-2d" / "ProjectSettings");
  std::ofstream(tdir / "a-2d" / "ProjectSettings" / "Project.json") << "{\"format\":1,\"name\":\"T\",\"target\":\"a\",\"scenes\":[]}\n";
  std::ofstream(home / "hub.json") << R"({"apis":[{"id":"a","available":true},{"id":"b","available":true}],
    "templates":[{"api":"a","kind":"2d","dir":"a-2d","available":true},{"api":"b","kind":"2d","dir":"b-2d","available":true}],
    "buildTargets":[{"id":"console","available":true}]})";
  HubData d;
  CHECK(d.load(home / "hub.json", tdir));
  CHECK(d.apiSelectable(0));
  CHECK(!d.apiSelectable(1));
  CHECK(d.templates[1].available && !d.templates[1].present && !d.templates[1].selectable());
  HubSettings s = settingsIn(home / "home");
  fs::path out;
  std::string err;
  CHECK(!createProject(d, s, "X", d.templates[1], "console", out, err));
  CHECK(err.find("b-2d") != std::string::npos);
  CHECK(createProject(d, s, "X", d.templates[0], "console", out, err));
}
