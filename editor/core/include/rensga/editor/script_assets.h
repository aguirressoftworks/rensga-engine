// MVP-54/56/57: project scripts on disk. Scan for Behaviours, create a script from a template, hash the sources
// the last build used, open a file in the user's editor. No ImGui, no console names.
#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace rensga {
namespace editor {

struct ScriptInfo {
  std::string name;              // the REFLECT'd class name (file name = class name, like Unity)
  std::filesystem::path path;    // the .cpp (or .h) that declares it
};

// Finds `struct X : Behaviour|Component { REFLECT(X) ...` in Assets/**/*.cpp and *.h, sorted by name.
std::vector<ScriptInfo> scanProjectScripts(const std::filesystem::path& projectRoot);

bool isCppIdentifier(const std::string& s);
// Empty string when `name` can be used for a new script, else the reason. `knownTypes` are the reflected type names
// already compiled in (a second type of the same name would clash at registration).
std::string validateScriptName(const std::filesystem::path& projectRoot, const std::string& name,
                               const std::set<std::string>& knownTypes);

enum class ScriptKind { Behaviour, Component };
// The text of a new script. Behaviour: Awake/Start/Update stubs and one SERIALIZE_FIELD with RANGE. Component: empty.
std::string scriptTemplate(const std::string& name, ScriptKind kind = ScriptKind::Behaviour);

// Writes Assets/Scripts/<name>.cpp and its .meta (new GUID), and lists the class in the project's ConsoleTarget
// allowedComponents so a scene that uses it cooks. Returns false and fills err.
bool createScriptAsset(const std::filesystem::path& projectRoot, const std::string& name, std::filesystem::path& outFile,
                       std::string& err, ScriptKind kind = ScriptKind::Behaviour);

// Adds `typeName` to ProjectSettings/ConsoleTarget.json allowedComponents (textual edit: the rest of the file is kept
// byte for byte). True when the name is in the list afterwards.
bool allowComponentInProject(const std::filesystem::path& projectRoot, const std::string& typeName);

// Content hash of Assets/Scripts/*.cpp and *.h (names and bytes). Empty when there is no such folder.
std::string hashProjectScripts(const std::filesystem::path& projectRoot);
std::filesystem::path scriptsHashFile(const std::filesystem::path& projectRoot);  // Library/Editor/scripts.hash
std::string readScriptsHash(const std::filesystem::path& projectRoot);
bool writeScriptsHash(const std::filesystem::path& projectRoot, const std::string& hash);

// Opening a file in the user's editor (openInExternalEditor, externalEditorCommand) lives in script_build.h.

}  // namespace editor
}  // namespace rensga
