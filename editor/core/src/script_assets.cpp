#include "rensga/editor/script_assets.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

#include <nlohmann/json.hpp>

#include "rensga/editor/scene_document.h"  // generateGuid

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::vector<ScriptInfo> scanProjectScripts(const fs::path& root) {
  std::vector<ScriptInfo> out;
  std::error_code ec;
  const fs::path assets = root / "Assets";
  if (!fs::is_directory(assets, ec)) return out;
  static const std::regex re(
      R"((?:struct|class)\s+(\w+)\s*:\s*(?:public\s+)?(?:rensga::)?(?:Behaviour|Component)\b[^{;]*\{\s*REFLECT\s*\(\s*(\w+)\s*\))");
  std::set<std::string> seen;
  for (fs::recursive_directory_iterator it(assets, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string ext = it->path().extension().string();
    if (ext != ".cpp" && ext != ".h") continue;
    const std::string text = slurp(it->path());
    for (std::sregex_iterator m(text.begin(), text.end(), re), e; m != e; ++m) {
      const std::string name = (*m)[2].str();
      if (seen.insert(name).second) out.push_back({name, it->path()});
    }
  }
  std::sort(out.begin(), out.end(), [](const ScriptInfo& a, const ScriptInfo& b) { return a.name < b.name; });
  return out;
}

bool isCppIdentifier(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
  for (char c : s)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
  return true;
}

static bool isKeyword(const std::string& s) {
  static const std::set<std::string> k = {
      "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const",
      "constexpr", "continue", "decltype", "default", "delete", "do", "double", "else", "enum", "explicit",
      "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
      "namespace", "new", "noexcept", "not", "nullptr", "operator", "or", "private", "protected", "public",
      "register", "return", "short", "signed", "sizeof", "static", "struct", "switch", "template", "this",
      "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
      "volatile", "while", "xor", "main"};
  return k.count(s) != 0;
}

static std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string validateScriptName(const fs::path& root, const std::string& name, const std::set<std::string>& knownTypes) {
  if (name.empty()) return "Enter a name.";
  if (!isCppIdentifier(name)) return "The name must be a C++ identifier (letters, digits, underscore; no leading digit).";
  if (isKeyword(name)) return "\"" + name + "\" is a C++ keyword.";
  if (knownTypes.count(name)) return "A type named " + name + " already exists.";
  std::error_code ec;
  const fs::path dir = root / "Assets" / "Scripts";
  const std::string want = lower(name);
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (lower(it->path().stem().string()) == want) return "A script or file named " + name + " already exists.";
  for (const ScriptInfo& s : scanProjectScripts(root))
    if (s.name == name) return "A script class named " + name + " already exists.";
  return "";
}

std::string scriptTemplate(const std::string& name, ScriptKind kind) {
  std::string s;
  s += "#include <rensga/Rensga.h>\n\nusing namespace rensga;\n\n";
  if (kind == ScriptKind::Component) {
    s += "// A data component: fields only, other scripts read it with GetComponent<" + name + ">().\n";
    s += "struct " + name + " : Component {\n";
    s += "  REFLECT(" + name + ")\n};\n";
    return s;
  }
  s += "struct " + name + " : Behaviour {\n";
  s += "  REFLECT(" + name + ")\n";
  s += "  HEADER(\"Settings\")\n";
  s += "  RANGE(0, 10)\n";
  s += "  SERIALIZE_FIELD(Scalar, speed, 1.0f)\n\n";
  s += "  void Awake() override {\n    // Called when the component is created.\n  }\n\n";
  s += "  void Start() override {\n    // Called once before the first Update.\n  }\n\n";
  s += "  void Update() override {\n    // Called every frame. Use Scalar, not float, for game values.\n  }\n";
  s += "};\n";
  return s;
}

bool allowComponentInProject(const fs::path& root, const std::string& typeName) {
  const fs::path file = root / "ProjectSettings" / "ConsoleTarget.json";
  std::string text = slurp(file);
  if (text.empty()) return false;
  try {
    nlohmann::json j = nlohmann::json::parse(text);
    for (const auto& n : j.value("allowedComponents", nlohmann::json::array()))
      if (n.is_string() && n.get<std::string>() == typeName) return true;
  } catch (...) { return false; }
  size_t key = text.find("\"allowedComponents\"");
  if (key == std::string::npos) return false;
  size_t open = text.find('[', key), close = open == std::string::npos ? open : text.find(']', open);
  if (close == std::string::npos) return false;
  size_t last = close;
  while (last > open + 1 && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
  const bool empty = last == open + 1;
  text.insert(last, empty ? "\"" + typeName + "\"" : ",\n    \"" + typeName + "\"");
  std::ofstream o(file, std::ios::binary | std::ios::trunc);
  o << text;
  return static_cast<bool>(o);
}

bool createScriptAsset(const fs::path& root, const std::string& name, fs::path& outFile, std::string& err,
                       ScriptKind kind) {
  const fs::path dir = root / "Assets" / "Scripts";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path cpp = dir / (name + ".cpp");
  if (fs::exists(cpp, ec)) { err = cpp.filename().string() + " already exists"; return false; }
  {
    std::ofstream o(cpp, std::ios::binary);
    o << scriptTemplate(name, kind);
    if (!o) { err = "cannot write " + cpp.string(); return false; }
  }
  {
    std::ofstream o(dir / (name + ".cpp.meta"), std::ios::binary);
    o << "{\n \"guid\": \"" << generateGuid() << "\",\n \"importSettings\": {}\n}\n";
    if (!o) { err = "cannot write the .meta for " + name; return false; }
  }
  allowComponentInProject(root, name);
  outFile = cpp;
  return true;
}

std::string hashProjectScripts(const fs::path& root) {
  const fs::path dir = root / "Assets" / "Scripts";
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return "";
  std::vector<fs::path> files;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string ext = it->path().extension().string();
    if (it->is_regular_file(ec) && (ext == ".cpp" || ext == ".h")) files.push_back(it->path());
  }
  std::sort(files.begin(), files.end());
  std::uint64_t h = 1469598103934665603ull;  // FNV-1a 64
  auto feed = [&h](const std::string& s) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  };
  for (const fs::path& f : files) {
    feed(f.filename().string());
    feed(std::string(1, '\0'));
    feed(slurp(f));
    feed(std::string(1, '\0'));
  }
  char buf[24];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
  return buf;
}

fs::path scriptsHashFile(const fs::path& root) { return root / "Library" / "Editor" / "scripts.hash"; }

std::string readScriptsHash(const fs::path& root) {
  std::string s = slurp(scriptsHashFile(root));
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

bool writeScriptsHash(const fs::path& root, const std::string& hash) {
  const fs::path f = scriptsHashFile(root);
  std::error_code ec;
  fs::create_directories(f.parent_path(), ec);
  std::ofstream o(f, std::ios::binary | std::ios::trunc);
  o << hash << "\n";
  return static_cast<bool>(o);
}

}  // namespace editor
}  // namespace rensga
