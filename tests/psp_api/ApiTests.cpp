#include <cstdint>

#include <rensga/Rensga.h>

using namespace rensga;

// F5.18 API tests. One Behaviour runs the checks in the first frames of the scene and prints one line per check:
//   psp_api: PASS <name>            psp_api: FAIL <name> (<why>)
// and at the end  psp_api: DONE pass=<n> fail=<m> platform=<name>.
// The same source runs on the host (rensga_headless, desktop) and in an emulator (PPSSPPHeadless); tools/ci/psp_api.sh checks both logs.
// Checks that depend on the platform say so and expect the right value for the platform they run on.
//
// failDemo (scene ApiTestsFail): adds ONE check that is wrong on the PSP only. It assumes a 64-bit `long`, which is true on the
// Linux host and false on the PSP (32-bit MIPS). The default scene leaves it off, so the default run is green on both; the script
// runs the demo scene on both to show that the PSP run is the one that fails.
struct ApiTests : Behaviour {
  REFLECT(ApiTests)
  SERIALIZE_FIELD(bool, failDemo, false)

  u32 pass = 0, fail = 0, frame = 0;
  bool finished = false;

  void check(const char* name, bool ok, const char* why = "") {
    if (ok) {
      ++pass;
      Debug::Log("psp_api: PASS %s", name);
    } else {
      ++fail;
      Debug::Log("psp_api: FAIL %s (%s)", name, why);
    }
  }

  bool onPsp() const { return Application::platform() == RuntimePlatform::Psp; }

  void Start() override {
    Debug::Log("psp_api: begin platform=%s failDemo=%d", Application::platformName(Application::platform()), failDemo ? 1 : 0);
    testApplication();
    testInput();
    testSave();
    testSystemInfo();
    testKeyboard();
    testAudioPriority();
    testLight();
    testPortability();
  }

  void Update() override {
    // Animation needs fixed steps to run: start it here and look at it a few frames later.
    ++frame;
    if (frame == 1) startAnimation();
    if (frame == 20 && !finished) {
      testAnimationAfter();
      finished = true;
      Debug::Log("psp_api: DONE pass=%u fail=%u platform=%s", static_cast<unsigned>(pass), static_cast<unsigned>(fail),
                 Application::platformName(Application::platform()));
    }
  }

  // ---- Application ------------------------------------------------------------------------------------------------------------
  void testApplication() {
    check("app_is_playing", Application::isPlaying());
    check("app_platform_name", Application::platformName(Application::platform()) != nullptr);
    // SetCpuClock: the PSP accepts 222 and 333 and rejects others; every other platform returns false and changes nothing.
    const bool want = onPsp();
    const bool a = Application::SetCpuClock(222);
    const bool b = Application::SetCpuClock(333);
    const bool c = Application::SetCpuClock(100);
    check("app_set_cpu_clock", a == want && b == want && !c, "222/333 must work on the PSP only, 100 never");
    const SystemLanguage l = Application::systemLanguage();
    check("app_system_language", l == SystemLanguage::Unknown || static_cast<u32>(l) <= static_cast<u32>(SystemLanguage::ChineseSimplified));
    const bool east = Application::confirmButtonEast();
    check("app_confirm_button_stable", east == Application::confirmButtonEast());
    check("app_target_frame_rate", Application::targetFrameRate() != 0);
  }

  // ---- Input -------------------------------------------------------------------------------------------------------------------
  void testInput() {
    InputAction* move = InputActions::Find("Move");
    check("input_action_found", move != nullptr, "ProjectSettings/InputActions.json has Move");
    if (move) {
      const Vector2 v = move->ReadValue<Vector2>();
      check("input_nub_idle", v.x == 0 && v.y == 0, "no pad is attached");
    }
    check("input_button_idle", !Input::GetButton("CameraLeft") && !Input::GetButton("ConfirmSouth"));
  }

  // ---- Save --------------------------------------------------------------------------------------------------------------------
  void testSave() {
    check("save_mode_default_auto", Save::mode() == SaveMode::Auto);
    if (Save::current() == nullptr) {
      Debug::Log("psp_api: SKIP save_roundtrip (no save store)");
      return;
    }
    const bool ok = Save::SaveInt("api_test", 1234567);
    check("save_int_roundtrip", ok && Save::LoadInt("api_test", -1) == 1234567);
    u8 data[24];
    for (u32 i = 0; i < sizeof(data); ++i) data[i] = static_cast<u8>(i * 7 + 3);
    u8 back[24] = {};
    const bool saved = Save::SaveFile("api_test_blob", data, sizeof(data));
    const i32 n = Save::LoadFile("api_test_blob", back, sizeof(back));
    bool same = saved && n == static_cast<i32>(sizeof(data));
    for (u32 i = 0; same && i < sizeof(data); ++i) same = back[i] == data[i];
    check("save_blob_roundtrip", same);
    check("save_missing", !Save::Exists("api_test_missing") && Save::LoadInt("api_test_missing", 77) == 77);
  }

  // ---- SystemInfo --------------------------------------------------------------------------------------------------------------
  void testSystemInfo() {
    const Scalar bl = SystemInfo::batteryLevel();
    check("sysinfo_battery_range", bl == -1.0f || (bl >= 0.0f && bl <= 1.0f), "-1 (unknown) or 0..1");
    const SystemInfo::BatteryStatus bs = SystemInfo::batteryStatus();
    check("sysinfo_battery_status", static_cast<u32>(bs) <= static_cast<u32>(SystemInfo::BatteryStatus::Full));
    check("sysinfo_memory_sizes", SystemInfo::systemMemorySize() >= 0 && SystemInfo::graphicsMemorySize() >= 0);
    if (onPsp()) {
      // the PSP reports its RAM and VRAM and what the game uses
      check("sysinfo_psp_memory_known", SystemInfo::systemMemorySize() > 0 && SystemInfo::graphicsMemorySize() > 0 &&
                                            SystemInfo::usedMemory() > 0 && SystemInfo::usedGraphicsMemory() > 0);
    }
  }

  // ---- TouchScreenKeyboard (availability only: opening it would block an unattended run) ------------------------------------------
  void testKeyboard() {
    check("osk_idle_status", TouchScreenKeyboard::status() == TouchScreenKeyboard::Status::None);
    check("osk_text_not_null", TouchScreenKeyboard::text() != nullptr);
  }

  // ---- Audio priority ----------------------------------------------------------------------------------------------------------
  void testAudioPriority() {
    GameObject* hi = GameObject::Find("SfxImportant");
    GameObject* def = GameObject::Find("SfxDefault");
    AudioSource* a = hi ? hi->GetComponent<AudioSource>() : nullptr;
    AudioSource* b = def ? def->GetComponent<AudioSource>() : nullptr;
    check("audio_priority_serialized", a && a->priority == 40, "scene value 40");
    check("audio_priority_default_128", b && b->priority == 128, "field missing in the scene: default 128");
  }

  // ---- Light and RenderSettings ------------------------------------------------------------------------------------------------
  void testLight() {
    GameObject* sun = GameObject::Find("Sun");
    Light* l = sun ? sun->GetComponent<Light>() : nullptr;
    check("light_sun_fields", l && l->type == 0 && l->intensity > 0.79f && l->intensity < 0.81f && l->renderMode == 0);
    GameObject* pt = GameObject::Find("PointRed");
    Light* p = pt ? pt->GetComponent<Light>() : nullptr;
    check("light_point_fields", p && p->type == 1 && p->range > 6.9f && p->range < 7.1f);
    GameObject* rs = GameObject::Find("RenderSettings");
    RenderSettings* r = rs ? rs->GetComponent<RenderSettings>() : nullptr;
    check("render_settings_fog", r && r->fogEnabled && r->fogEnd > r->fogStart);
  }

  // ---- Animation ---------------------------------------------------------------------------------------------------------------
  Animation* anim() {
    GameObject* c = GameObject::Find("Character");
    return c ? c->GetComponent<Animation>() : nullptr;
  }
  void startAnimation() {
    Animation* a = anim();
    check("anim_found", a != nullptr);
    if (!a) return;
    check("anim_idle_before_play", !a->isPlaying(), "playAutomatically is off in this scene");
    a->Play();
    check("anim_playing_after_play", a->isPlaying());
  }
  void testAnimationAfter() {
    Animation* a = anim();
    if (!a) return;
    check("anim_loops", a->isPlaying(), "wrapMode loop keeps the clip playing");
    a->Stop();
    check("anim_stop", !a->isPlaying());
  }

  // ---- Portability -------------------------------------------------------------------------------------------------------------
  void testPortability() {
    // Checks that hold on every profile. They compare with tolerance (libm may differ in the last bit between a host and the PSP).
    const Scalar s = Mathf::Sin(0.5f);
    check("math_sin_close", s > 0.47942f && s < 0.47943f);
    alignas(16) static float v4[4];
    check("align_16", (reinterpret_cast<uintptr_t>(v4) & 15u) == 0);
    check("sizeof_scalar_vector3", sizeof(Scalar) == 4 && sizeof(Vector3) == 12);
    if (failDemo) {
      // WRONG on the PSP on purpose: a `long` is 8 bytes on the Linux host and 4 on the PSP.
      check("demo_long_is_64_bit", sizeof(long) == 8, "sizeof(long) is 4 on the PSP");
    }
  }
};
