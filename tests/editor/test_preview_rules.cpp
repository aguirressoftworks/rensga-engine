// F5.06 / F5.07 / F5.08: the target's rules as data (preview, import, inspector) and the console-form cook that feeds the
// Game view's budget bars / decoded textures and the Inspector's validation list. No console is named: a target is a
// ConsoleTargetData plus the cooker plugin id.
#include <filesystem>
#include <fstream>

#include "rensga/editor/editor_world.h"
#include "rensga/editor/import_settings.h"
#include "rensga/editor/inspector_model.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;

static ConsoleTargetData limitedTarget() {
  ConsoleTargetData ct;
  ct.width = 480; ct.height = 272;
  ct.fbBpp = 16;
  ct.maxLightsPerObject = 4;
  ct.maxTextureSize = 512;
  ct.audioSampleRate = 22050;
  ct.textureFormats = {"clut4", "clut8", "rgba8"};
  ct.features = {"lighting", "textureSwizzle"};  // no fog, no skinning
  return ct;
}

TEST(preview_rules_fill_from_the_console_target) {
  const PreviewRules r = effectivePreviewRules(nullptr, limitedTarget());
  CHECK_EQ(r.width, 480);
  CHECK_EQ(r.colorBits, 16);
  CHECK(r.dither);
  CHECK_EQ(r.maxLightsPerObject, 4);
  ConsoleTargetData full;
  full.width = 640; full.height = 448; full.fbBpp = 32;
  const PreviewRules f = effectivePreviewRules(nullptr, full);
  CHECK_EQ(f.colorBits, 32);
  CHECK(!f.dither);
  CHECK_EQ(f.maxLightsPerObject, 0);
}

TEST(import_rules_follow_limits_and_formats) {
  const ImportRules r = effectiveImportRules(nullptr, limitedTarget());
  CHECK_EQ(r.maxTextureSize, 512u);
  CHECK(r.pow2Warn);
  CHECK_EQ(r.clutDepths.size(), 2u);
  CHECK_EQ(r.audioRates.size(), 2u);
  CHECK_EQ(r.audioRates[0], 22050);
  CHECK_EQ(r.audioRates[1], 11025);
  // the schema stops at the target's size limit and gains the rates
  const std::vector<ImportField> tex = importFields(AssetKind::Texture, limitedTarget(), true, &r);
  for (const ImportField& f : tex)
    if (f.key == "maxSize") CHECK(std::find(f.options.begin(), f.options.end(), "1024") == f.options.end());
  const std::vector<ImportField> plain = importFields(AssetKind::Texture, limitedTarget(), true);
  for (const ImportField& f : plain)
    if (f.key == "maxSize") CHECK(std::find(f.options.begin(), f.options.end(), "1024") != f.options.end());
}

TEST(texture_cost_lists_downscale_and_power_of_two_notes) {
  ImportRules rules;
  rules.maxTextureSize = 512;
  rules.pow2Warn = true;
  ImportOutcome o;
  o.ok = true;
  o.srcWidth = 1024; o.srcHeight = 600;
  o.tex.width = 512; o.tex.height = 293; o.tex.format = 1; o.tex.palette.assign(16, 0);
  o.storedWidth = 512; o.storedHeight = 512;
  o.cookedBytes = 512 * 512 / 2 + 64;
  const AssetCost c = textureCost(o, rules);
  CHECK(c.valid);
  CHECK_EQ(c.vramBytes, o.cookedBytes);
  CHECK_EQ(c.notes.size(), 2u);
  CHECK(c.notes[0].find("downscale 1024 x 600 to 512 x 293") != std::string::npos);
  CHECK(c.notes[1].find("not a power of two") != std::string::npos);
  o.srcWidth = 64; o.srcHeight = 64; o.tex.width = 64; o.tex.height = 64; o.storedWidth = 64; o.storedHeight = 64;
  CHECK(textureCost(o, rules).notes.empty());
}

TEST(inspector_gates_fields_of_missing_features) {
  const InspectorRules r = effectiveInspectorRules(nullptr, limitedTarget(), fs::path(RENSGA_TEST_TMP_DIR) / "no_such_project", "Mini");
  CHECK(fieldGreyReason(r, "SkinnedMeshRenderer", "*").find("not available on Mini") != std::string::npos);
  CHECK(fieldGreyReason(r, "SkinnedMeshRenderer", "mesh").find("Skeletal animation") != std::string::npos);
  CHECK(fieldGreyReason(r, "RenderSettings", "fogEnd").find("fade to the background color") != std::string::npos);
  CHECK_EQ(fieldGreyReason(r, "RenderSettings", "ambient"), std::string());  // lighting is there
  CHECK_EQ(fieldGreyReason(r, "Light", "intensity"), std::string());
  CHECK_EQ(fieldGreyReason(r, "Light", "*"), std::string());
  ConsoleTargetData none;  // a profile that lists no features gates nothing
  CHECK(effectiveInspectorRules(nullptr, none, fs::path(RENSGA_TEST_TMP_DIR) / "no_such_project", "X").fieldRules.empty());
}

TEST(inspector_reads_field_rules_from_the_console_target_file) {
  const fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / "field_rules";
  fs::remove_all(d);
  fs::create_directories(d / "ProjectSettings");
  std::ofstream(d / "ProjectSettings" / "ConsoleTarget.json")
      << R"({"format":1,"fieldRules":[{"component":"Light","field":"shadows","reason":"This target draws no shadows."},{"component":"Foo","reason":"no Foo"}]})";
  const InspectorRules r = effectiveInspectorRules(nullptr, ConsoleTargetData(), d, "T");
  CHECK_EQ(fieldGreyReason(r, "Light", "shadows"), std::string("This target draws no shadows."));
  CHECK_EQ(fieldGreyReason(r, "Light", "intensity"), std::string());
  CHECK_EQ(fieldGreyReason(r, "Foo", "anything"), std::string("no Foo"));  // field defaults to the whole component
}

TEST(cook_messages_become_selectable_findings) {
  const char* guid = "11111111-2222-4333-8444-555555555555";
  nlohmann::ordered_json scene = nlohmann::ordered_json::parse(
      std::string(R"({"objects":[{"guid":")") + guid + R"(","name":"Lamp"},{"guid":"aaaaaaaa-2222-4333-8444-555555555555","name":"Rock"}]})");
  std::vector<CookMessage> msgs;
  msgs.push_back({CookMessage::Warning, std::string("object 'Lamp' (guid ") + guid + ") is reached by 6 lights, the x profile draws at most 4 per object", "Assets/s.scene", ""});
  msgs.push_back({CookMessage::Warning, "object 'Rock' is reached by 5 lights", "", ""});
  msgs.push_back({CookMessage::Error, "skin 'Assets/a.gltf' has 70 joints, over the limit of 64", "Assets/a.gltf", ""});
  msgs.push_back({CookMessage::Warning, "texture 'Assets/t.png' downscaled from 1024x1024 to 512x512", "Assets/t.png", ""});
  msgs.push_back({CookMessage::Info, "cook: cooked 1, cached 0", "", ""});
  const std::vector<ValidationItem> v = classifyCookMessages(msgs, scene);
  CHECK_EQ(v.size(), 4u);  // Info is not a finding
  CHECK_EQ(v[0].category, std::string("lights"));
  CHECK_EQ(v[0].objectName, std::string("Lamp"));
  CHECK_EQ(v[0].objectGuid, std::string(guid));
  CHECK_EQ(v[1].objectName, std::string("Rock"));  // by quoted name when no guid is in the text
  CHECK_EQ(v[2].category, std::string("skinning"));
  CHECK(v[2].severity == ValidationItem::Error);
  CHECK_EQ(v[3].category, std::string("texture"));
}

#ifdef RENSGA_ROOM_DIR
TEST(console_form_cook_of_the_lit_room_lists_the_light_findings) {
  const fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / "room_preview";
  fs::remove_all(d);
  fs::create_directories(d);
  fs::copy(RENSGA_ROOM_DIR, d, fs::copy_options::recursive);
  fs::remove_all(d / "Library");
  const fs::path sceneFile = d / "Assets" / "Scenes" / "Lights12.scene";
  std::ifstream in(sceneFile);
  nlohmann::ordered_json doc = nlohmann::ordered_json::parse(in);
  ConsolePreview cp;
  const bool ok = cookConsolePreview(d, "psp", sceneFile, doc, cp);
  CHECK(ok);
  if (!ok) { std::fprintf(stderr, "preview error: %s\n", cp.error.c_str()); return; }
  int lights = 0, named = 0;
  for (const ValidationItem& it : cp.items)
    if (it.category == "lights") { ++lights; named += !it.objectGuid.empty(); }
  CHECK(lights >= 3);
  CHECK_EQ(lights, named);  // every light finding names an object of the scene document
  CHECK(cp.textures.size() >= 2);
  for (const ConsolePreview::Texture& t : cp.textures)
    CHECK(!t.rgba.rgba.empty() && t.rgba.width > 0);  // decoded through the target plugin
  CHECK(cp.packBytes > 0 && cp.triangles > 0 && cp.textureBytes > 0);
}
#endif
