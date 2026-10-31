// F6 contract (C0): the allocation-free helpers that are real (StrBuf / Format, ObjectPool, GameObjectPool, MaterialPropertyBlock,
// LayerMask, Application::profile) and the safe defaults of every stubbed API call (rensga/Rensga.h). When a Stage 1 to 3 agent
// implements a stub, the matching check here moves to that agent's own test file.
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga_test.h"

using namespace rensga;

TEST(f6_strbuf_and_format) {
  StrBuf<16> s;
  s << "Score " << 1250 << " x" << 3u;
  CHECK(strcmp(s.c_str(), "Score 1250 x3") == 0);
  CHECK(!s.truncated());
  StrBuf<8> t;
  t << "abcdefghij";
  CHECK(strcmp(t.c_str(), "abcdefg") == 0 && t.truncated() && t.size() == 7);  // cut, NUL terminated, never past N
  StrBuf<32> n;
  n << -42 << ' ' << Fixed(3.14159f, 3) << ' ' << Fixed(-0.5f, 1) << ' ' << true << ' ' << 2.5f;
  CHECK(strcmp(n.c_str(), "-42 3.142 -0.5 true 2.50") == 0);
  StrBuf<16> z;
  z << Fixed(-0.001f, 2);
  CHECK(strcmp(z.c_str(), "0.00") == 0);  // no "-0.00"
  char buf[32];
  CHECK_EQ(Format(buf, sizeof buf, "HP {0}/{1} {1}", 7, 100), 12u);
  CHECK(strcmp(buf, "HP 7/100 100") == 0);
  CHECK(Format(buf, sizeof buf, "{{x}} {0} {2}", "a") > 0 && strcmp(buf, "{x} a ") == 0);  // {{ and }} are braces, a missing argument is empty
  char tiny[5];
  CHECK_EQ(Format(tiny, sizeof tiny, "Score {0}", 12345), 4u);
  CHECK(strcmp(tiny, "Scor") == 0);
  UIText text;
  text.SetText(s);  // a StrBuf converts to const char*
  CHECK(strcmp(text.text, "Score 1250 x3") == 0);
}

TEST(f6_object_pool) {
  ObjectPool<i32, 3> pool;
  CHECK_EQ(pool.capacity(), 3u);
  i32* a = pool.Get();
  i32* b = pool.Get();
  i32* c = pool.Get();
  CHECK(a && b && c && a != b && b != c);
  CHECK(pool.Get() == nullptr);  // exhausted
  CHECK_EQ(pool.activeCount(), 3u);
  *b = 99;
  CHECK(pool.Release(b));
  CHECK(!pool.Release(b));       // double release refused
  i32 outside = 0;
  CHECK(!pool.Release(&outside));
  i32* d = pool.Get();
  CHECK(d == b && *d == 0);      // the slot is reused and reset
  u32 n = 0;
  pool.ForEachActive([&](i32&) { ++n; });
  CHECK_EQ(n, 3u);
  pool.Reset();
  CHECK_EQ(pool.availableCount(), 3u);
}

TEST(f6_game_object_pool) {
  Arena arena;
  arena.create(1 << 20);
  World world;
  WorldConfig cfg;
  cfg.maxObjects = 16;
  cfg.maxComponentsPerType = 16;
  world.init(cfg, arena);
  GameObject* prefab = world.createObject("Bullet");
  GameObjectPool<3> pool;
  CHECK_EQ(pool.Init(prefab), 3u);
  CHECK_EQ(pool.availableCount(), 3u);
  GameObject* a = pool.Get(Vector3(1, 2, 3), Quaternion());
  CHECK(a != nullptr && a->activeSelf());
  CHECK(a->transform.position().y == 2);
  CHECK_EQ(pool.availableCount(), 2u);
  pool.Get();
  pool.Get();
  CHECK(pool.Get() == nullptr);
  pool.Release(a);
  CHECK(!a->activeSelf());
  CHECK(pool.Get() == a);
  world.shutdown();
}

TEST(f6_material_block_and_layer_mask) {
  MeshRenderer r;
  CHECK(r.GetPropertyBlock().isEmpty());
  MaterialPropertyBlock b;
  b.SetColor(Color32(10, 20, 30, 40));
  Guid g;
  Guid::parse("00000000-0000-4000-8000-0000000000aa", g);
  b.SetTexture(g);
  r.SetPropertyBlock(b);
  CHECK(r.GetPropertyBlock().hasColor && r.GetPropertyBlock().hasTexture && r.GetPropertyBlock().color.g == 20);
  MeshRenderer copy;
  copy.OnCloned(&r);
  CHECK(copy.GetPropertyBlock().texture == g);  // Instantiate copies the block
  r.ClearPropertyBlock();
  CHECK(r.GetPropertyBlock().isEmpty());
  LayerMask m = LayerMask::Layer(3);
  CHECK(m.Contains(3) && !m.Contains(4) && !LayerMask::Layer(40).Contains(0));
  u32 raw = m;  // implicit
  CHECK_EQ(raw, 8u);
  CHECK(LayerMask::All().Contains(31) && !LayerMask::None().Contains(0));
  Ray ray(Vector3(0, 1, 0), Vector3(0, 0, 2));
  CHECK(ray.GetPoint(2).z == 4);
}

TEST(f6_application_profile) {
  Application::reset();
  CHECK(Application::profile() == ConsoleProfile::Unknown);
  Application::setPlatform(RuntimePlatform::Psp);
  CHECK(Application::profile() == ConsoleProfile::Psp);  // the console platform implies its profile
  Application::setProfile(ConsoleProfile::Ps2);
  CHECK(Application::profile() == ConsoleProfile::Ps2 && strcmp(Application::profileName(Application::profile()), "PS2") == 0);
  Application::reset();
  CHECK(Application::profile() == ConsoleProfile::Unknown);
}

// Every stub returns a safe default (no hit, zero, invalid handle, false) and none crashes.
TEST(f6_stubs_return_safe_defaults) {
  RaycastHit hit;
  CHECK(!Physics::Raycast(Vector3(), Vector3(0, 0, 1)));
  CHECK(!Physics::Raycast(Ray(), &hit));
  CHECK(!Physics::Linecast(Vector3(), Vector3(1, 1, 1)));
  CHECK(!Physics::SphereCast(Vector3(), 1.0f, Vector3(0, 0, 1)));
  GameObject* objs[4];
  CHECK_EQ(Physics::OverlapSphere(Vector3(), 5.0f, objs, 4), 0u);
  CHECK(!Physics2D::Raycast(Vector2(), Vector2(1, 0)).hit());
  CHECK(Physics2D::OverlapCircle(Vector2(), 4.0f) == nullptr);
  CHECK_EQ(Physics2D::OverlapBoxAll(Vector2(), Vector2(8, 8), objs, 4), 0u);
  CHECK(Physics::gravity().y < 0);

  Rigidbody rb;
  rb.AddForce(Vector3(1, 0, 0), ForceMode::Impulse);
  rb.setVelocity(Vector3(0, 3, 0));
  CHECK(rb.velocity().y == 3);
  Camera cam;
  CHECK(Camera::main() == nullptr);
  CHECK(cam.ScreenToWorldPoint(Vector3(1, 2, 3)) == Vector3());
  ParticleSystem ps;
  ps.Play();
  ps.Emit(5);
  CHECK(!ps.isPlaying() && ps.particleCount() == 0);
  Animator an;
  an.SetFloat("speed", 1.0f);
  an.SetTrigger(Animator::StringToHash("jump"));
  CHECK(!an.Play("Run") && !an.CrossFade("Run", 0.1f));
  CHECK(an.GetCurrentStateInfo().nameHash == 0);
  CHECK_EQ(Animator::StringToHash("Run"), fnv1a32("Run"));
  static int events = 0;
  AnimationEvents::setHandler([](GameObject*, const AnimationEvent& e) { events += e.intParameter; });
  AnimationEvent ev;
  ev.intParameter = 4;
  AnimationEvents::dispatch(nullptr, ev);
  AnimationEvents::setHandler(nullptr);
  CHECK_EQ(events, 4);
  AudioMixerGroups::SetVolume(AudioGroup::Music, 0.5f);
  CHECK_EQ(AudioMixerGroups::GetVolume(AudioGroup::Music), 0.5f);
  AudioMixerGroups::ResetAll();
  CHECK_EQ(AudioMixerGroups::GetVolume(AudioGroup::Music), 1.0f);
  CHECK(!AudioSource::PlayClipAtPoint(Guid(), Vector3()));

  CHECK(!SceneManager::LoadSceneAsync(0).IsValid());
  CHECK(!SceneManager::LoadScene(0, LoadSceneMode::Additive));
  CHECK(!SceneManager::GetActiveScene().IsValid() || SceneManager::GetActiveScene().name() != nullptr);
  AsyncOperation op;
  CHECK(!op.isDone() && op.progress() == 0 && op.allowSceneActivation());
  CHECK(!Resources::Load<ResAudioClip>("sfx/jump").valid());
  Handle<Light> data = Resources::Load<Light>("data/torch");
  CHECK(data.get() == nullptr);
  CHECK_EQ(PlayerPrefs::GetInt("volume", 7), 7);
  CHECK(!PlayerPrefs::HasKey("volume") && !PlayerPrefs::Save());
  CHECK(strcmp(PlayerPrefs::GetString("name", "anon"), "anon") == 0);
  SaveSystem::SlotInfo info;
  CHECK(!SaveSystem::Exists(0) && SaveSystem::Read(0, 1, nullptr, 0) == -1 && !SaveSystem::GetInfo(0, &info) && !info.exists);
  Profiler::BeginSample("x");
  Profiler::EndSample();
  RENSGA_PROFILE_SCOPE("scope");
  CHECK_EQ(Budget::memoryLimit() + Budget::vramUsed() + Budget::percentUsed(), 0u);
  Slider sl;
  sl.minValue = 0;
  sl.maxValue = 10;
  sl.SetValue(25);
  CHECK(sl.GetValue() == 10 && sl.normalizedValue() == 1.0f);
  Toggle tg;
  tg.SetIsOn(true);
  CHECK(tg.GetIsOn());
  CHECK(EventSystem::current() == nullptr);
}
