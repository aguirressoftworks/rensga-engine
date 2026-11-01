#include <rensga/Rensga.h>
#include <stdio.h>

using namespace rensga;

// F6.13 pool_spawner: a projectile spawner with and without a GameObjectPool, measured with Profiler samples.
//   pooled = false: every shot is Instantiate(template) and Destroy(shot) after its life (object and component slots churn).
//   pooled = true : GameObjectPool<96> makes 96 inactive clones once in Start; Get / Release only flip SetActive.
// "Spawn" and "Move" are Profiler samples; the runtime prints them every 60 frames as
//   rensga: prof sample name=Spawn frames=60 calls=<n> avg_us=<per frame> max_us=<worst frame>
// The script logs a summary line when it ends the run (runFrames) and quits:
//   rensga: pool: mode=pooled|plain frames=<n> spawned=<n> refused=<n> live_max=<n> objects=<n>
struct ProjectileSpawner : Behaviour {
  REFLECT(ProjectileSpawner)
  TOOLTIP("true: reuse a GameObjectPool; false: Instantiate and Destroy every shot.")
  SERIALIZE_FIELD(bool, pooled, false)
  TOOLTIP("Shots started per frame.")
  SERIALIZE_FIELD(i32, perFrame, 2)
  TOOLTIP("Frames a shot lives. Live shots = perFrame x lifeFrames, at most 96.")
  SERIALIZE_FIELD(i32, lifeFrames, 30)
  TOOLTIP("Frames before the run ends (the script quits the game).")
  SERIALIZE_FIELD(i32, runFrames, 310)

  static constexpr u32 kCap = 96;
  struct Live {
    Ref<GameObject> go;
    i32 age = 0;
    Vector3 pos;
    Vector3 vel;
  };
  Live live[kCap];
  u32 liveCount = 0, liveMax = 0;
  GameObjectPool<kCap> pool;
  GameObject* templ = nullptr;
  i32 frame = 0, spawned = 0, refused = 0;
  u32 seed = 12345;

  Scalar Rand01() {  // LCG: the same shots on every platform
    seed = seed * 1664525u + 1013904223u;
    return static_cast<Scalar>((seed >> 8) & 0xFFFF) / 65535.0f;
  }

  void Start() override {
    templ = GameObject::Find("ShotTemplate");
    if (pooled && templ) {
      const u32 made = pool.Init(templ);
      Debug::Log("rensga: pool: init made=%u of %u objects=%u", made, kCap, gameObject()->world->objectCount());
    }
    Debug::Log("rensga: pool: start mode=%s perFrame=%d life=%d", pooled ? "pooled" : "plain", perFrame, lifeFrames);
  }

  void Update() override {
    ++frame;
    Profiler::BeginSample("Spawn");
    for (i32 i = 0; i < perFrame && liveCount < kCap; ++i) {
      const Vector3 p(-5.0f + 10.0f * Rand01(), -4.0f, 0.0f);
      GameObject* go = nullptr;
      if (pooled) {
        go = pool.Get(p);
      } else if (templ) {
        go = Instantiate(templ, p, Quaternion());
        if (go) go->SetActive(true);
      }
      if (!go) { ++refused; continue; }
      Live& l = live[liveCount++];
      l.go = go->ref();
      l.age = 0;
      l.pos = p;
      l.vel = Vector3(0.0f, 0.12f + 0.05f * Rand01(), 0.0f);
      ++spawned;
    }
    if (liveCount > liveMax) liveMax = liveCount;
    Profiler::EndSample();

    Profiler::BeginSample("Move");
    for (u32 i = 0; i < liveCount;) {
      Live& l = live[i];
      GameObject* go = l.go.get();
      ++l.age;
      if (go) {
        l.pos = l.pos + l.vel;
        go->transform.setPosition(l.pos);
      }
      if (!go || l.age >= lifeFrames) {
        if (go) {
          if (pooled) pool.Release(go);
          else Destroy(go);
        }
        live[i] = live[--liveCount];
        continue;
      }
      ++i;
    }
    Profiler::EndSample();

    if (frame >= runFrames) {
      Debug::Log("rensga: pool: mode=%s frames=%d spawned=%d refused=%d live_max=%u objects=%u", pooled ? "pooled" : "plain", frame, spawned, refused,
                 liveMax, gameObject()->world->objectCount());
      Application::Quit();
    }
  }
};
