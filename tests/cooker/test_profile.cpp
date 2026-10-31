// F5.12 / F5.17 cooker side: per-profile features and limits are data, components that need a missing feature are refused with a
// hint, the voice-budget warning names the scene, and audio is resampled to the target rate with a windowed-sinc filter.
#include <math.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "rensga/cooker/assets.h"
#include "rensga/cooker/cooker.h"
#include "rensga/cooker/resample.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
ConsoleTargetData loadProfile(const char* profile, const json& extra = json::object()) {
  const fs::path dir = fs::temp_directory_path() / (std::string("rensga_profile_") + profile);
  fs::create_directories(dir);
  json j = {{"format", 1}, {"profile", profile}, {"scalar", "float"}, {"resolution", {480, 272}},
            {"budgets", {{"ramBytes", 1048576}, {"vramBytes", 1048576}, {"trianglesPerFrame", 1000}, {"maxObjects", 100},
                         {"maxComponentsPerType", 100}}},
            {"allowedComponents", json::array()}};
  for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
  std::ofstream(dir / "ConsoleTarget.json") << j.dump();
  return loadConsoleTarget(dir / "ConsoleTarget.json");
}
SceneBudgetInput scene(const char* type, uint64_t count) {
  SceneBudgetInput in;
  in.sceneAsset = "Assets/Scenes/Level1.scene";
  in.sceneGuid = "11111111-2222-3333-4444-555555555555";
  in.componentsPerType.push_back({type, count});
  return in;
}
}  // namespace

TEST(profile_defaults_per_console) {
  ConsoleTargetData psp = loadProfile("psp"), ps2 = loadProfile("ps2"), psx = loadProfile("psx");
  CHECK_EQ(psp.maxLightsPerObject, 4u);
  CHECK_EQ(psp.maxBonesPerDraw, 8u);
  CHECK_EQ(psp.features.size(), 8u);
  CHECK(psp.hasFeature("osk") && psp.hasFeature("cpuClock") && psp.hasFeature("battery") && psp.hasFeature("saveDialog"));
  CHECK(ps2.hasFeature("lighting") && ps2.hasFeature("skinning") && ps2.hasFeature("fog"));
  CHECK(!ps2.hasFeature("osk") && !ps2.hasFeature("cpuClock") && !ps2.hasFeature("battery") && !ps2.hasFeature("saveDialog"));
  CHECK_EQ(psx.maxLightsPerObject, 3u);  // GTE
  CHECK(psx.hasFeature("lighting") && !psx.hasFeature("skinning"));
  CHECK_EQ(psx.ramLimitBytes, 2097152ull);
  // The project can still override both.
  ConsoleTargetData bare = loadProfile("psp", {{"features", json::array()}, {"limits", {{"maxLightsPerObject", 2}}}});
  CHECK(bare.features.empty());
  CHECK_EQ(bare.maxLightsPerObject, 2u);
}

TEST(profile_generated_header_carries_features) {
  const std::string psp = generateConsoleTargetHeader(loadProfile("psp"));
  const std::string psx = generateConsoleTargetHeader(loadProfile("psx"));
  CHECK(psp.find("#define ENGINE_FEATURE_CPUCLOCK 1") != std::string::npos);
  CHECK(psp.find("#define ENGINE_FEATURE_SKINNING 1") != std::string::npos);
  CHECK(psx.find("ENGINE_FEATURE_CPUCLOCK") == std::string::npos);
  CHECK(psx.find("#define ENGINE_FEATURE_LIGHTING 1") != std::string::npos);
}

TEST(profile_validator_refuses_components_without_their_feature) {
  ConsoleTargetData psp = loadProfile("psp"), psx = loadProfile("psx");
  std::vector<CookMessage> errs;
  CHECK(validateBudgets(psp, scene("SkinnedMeshRenderer", 2), errs));
  CHECK(errs.empty());
  CHECK(!validateBudgets(psx, scene("SkinnedMeshRenderer", 2), errs));
  CHECK_EQ(errs.size(), 1u);
  CHECK(errs[0].text.find("Level1.scene") != std::string::npos);
  CHECK(errs[0].text.find("11111111-2222-3333-4444-555555555555") != std::string::npos);
  CHECK(errs[0].text.find("'skinning'") != std::string::npos && errs[0].text.find("psx") != std::string::npos);
  errs.clear();
  CHECK(!validateBudgets(psx, scene("Animation", 1), errs));
  CHECK(validateBudgets(psx, scene("Light", 3), errs));  // GTE lighting exists
  CHECK(validateBudgets(psx, scene("RenderSettings", 1), errs));
  ConsoleTargetData none = loadProfile("psp", {{"features", json::array()}});
  CHECK(!validateBudgets(none, scene("Light", 1), errs));
  CHECK(!validateBudgets(none, scene("RenderSettings", 1), errs));
}

TEST(profile_audio_voice_warning_names_the_scene) {
  ConsoleTargetData t = loadProfile("psp", {{"audio", {{"sampleRate", 22050}, {"voices", 8}}}});
  SceneBudgetInput in = scene("AudioSource", 20);
  std::vector<CookMessage> w;
  in.audioStartingSources = 8;
  CHECK(warnAudioVoices(t, in, w));
  CHECK(w.empty());
  in.audioStartingSources = 9;
  CHECK(!warnAudioVoices(t, in, w));
  CHECK_EQ(w.size(), 1u);
  CHECK(w[0].severity == CookMessage::Warning);
  CHECK(w[0].text.find("Assets/Scenes/Level1.scene") != std::string::npos);
  CHECK(w[0].text.find("9 AudioSources") != std::string::npos && w[0].text.find("8 voices") != std::string::npos);
  CHECK(w[0].assetGuid == in.sceneGuid);
}

namespace {
PcmAudio tone(uint32_t rate, double hz, uint32_t frames, uint32_t channels = 1) {
  PcmAudio p;
  p.sampleRate = rate;
  p.channels = channels;
  p.samples.resize(static_cast<size_t>(frames) * channels);
  for (uint32_t i = 0; i < frames; ++i)
    for (uint32_t c = 0; c < channels; ++c)
      p.samples[i * channels + c] = static_cast<int16_t>(lround(10000.0 * sin(2 * M_PI * hz * i / rate)));
  return p;
}
double rms(const PcmAudio& p, uint32_t skipFrames) {
  double s = 0;
  size_t n = 0;
  for (size_t i = skipFrames * p.channels; i + skipFrames * p.channels < p.samples.size(); ++i, ++n) s += double(p.samples[i]) * p.samples[i];
  return sqrt(s / n);
}
}  // namespace

TEST(profile_resample_keeps_the_passband_and_removes_what_would_alias) {
  PcmAudio low = tone(44100, 1000, 44100);
  audioResampleSinc(low, 22050);
  CHECK_EQ(low.sampleRate, 22050u);
  CHECK_EQ(low.frames(), 22050u);
  CHECK_NEAR(rms(low, 200), 10000.0 / sqrt(2.0), 150.0);  // 1 kHz passes at full level
  PcmAudio high = tone(44100, 16000, 44100);              // above the new Nyquist (11025): must be filtered out, not folded to 6 kHz
  audioResampleSinc(high, 22050);
  CHECK(rms(high, 200) < 100.0);
  PcmAudio st = tone(44100, 2000, 8000, 2);
  audioResampleSinc(st, 22050);
  CHECK_EQ(st.channels, 2u);
  CHECK_EQ(st.frames(), 4000u);
  PcmAudio dc;
  dc.sampleRate = 48000;
  dc.channels = 1;
  dc.samples.assign(4800, 12345);
  audioResampleSinc(dc, 22050);
  CHECK_EQ(dc.samples.front(), 12345);  // unity gain at DC, edges repeat
  CHECK_EQ(dc.samples.back(), 12345);
  PcmAudio same = tone(22050, 440, 1000);
  const auto before = same.samples;
  audioResampleSinc(same, 22050);
  CHECK(same.samples == before);
  audioResampleSinc(same, 44100);  // never raises the rate
  CHECK_EQ(same.sampleRate, 22050u);
}

TEST(profile_target_rate_joins_the_import_settings) {
  CHECK_EQ(json::parse(withTargetAudioRate("", 22050))["sampleRate"].get<int>(), 22050);
  json j = json::parse(withTargetAudioRate("{\"loop\":true}", 22050));
  CHECK(j["loop"].get<bool>() && j["sampleRate"].get<int>() == 22050);
  CHECK_EQ(json::parse(withTargetAudioRate("{\"sampleRate\":11025}", 22050))["sampleRate"].get<int>(), 11025);  // the asset's own wins
  CHECK_EQ(withTargetAudioRate("{\"loop\":true}", 0), std::string("{\"loop\":true}"));
}
