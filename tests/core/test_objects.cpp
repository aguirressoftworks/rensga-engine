// API 2: GameObject / Transform / Instantiate / Destroy / Debug / Application, on a bare World.
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

struct ObjFixture {
  Arena arena;
  World world;
  GameLoop loop;
  VirtualGamepad pad;
  explicit ObjFixture(u32 maxObjects = 32, u32 maxComps = 8) {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = maxObjects;
    c.maxComponentsPerType = maxComps;
    world.init(c, arena);
    loop.init(world);
  }
  ~ObjFixture() { world.shutdown(); }
  void frames(int n) {
    for (int i = 0; i < n; ++i) loop.frame(1.0f / 60.0f, pad);
  }
};

bool near(Scalar a, Scalar b) { return (a - b) < 1e-3f && (b - a) < 1e-3f; }
bool near(const Vector3& a, const Vector3& b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }

struct ObjMarker : Behaviour {
  REFLECT(ObjMarker)
  SERIALIZE_FIELD(i32, value, 7)
  SERIALIZE_FIELD(Scalar, speed, 1.5f)
};
struct ObjOther : Behaviour {
  REFLECT(ObjOther)
};

}  // namespace

TEST(objects_tag_layer_find) {
  ObjFixture f;
  GameObject* a = f.world.createObject("Player");
  GameObject* b = f.world.createObject("Enemy1");
  GameObject* c = f.world.createObject("Enemy2");
  CHECK(a->CompareTag("Untagged"));
  b->SetTag("Enemy");
  c->SetTag("Enemy");
  c->layer = 5;
  CHECK(b->CompareTag("Enemy") && !b->CompareTag("Player"));
  CHECK(GameObject::Find("Player") == a);
  CHECK(GameObject::Find("Nobody") == nullptr);
  CHECK(GameObject::FindWithTag("Enemy") == b);
  GameObject* found[4];
  CHECK_EQ(GameObject::FindGameObjectsWithTag("Enemy", found, 4), 2u);
  CHECK(found[0] == b && found[1] == c);
  CHECK_EQ(GameObject::FindGameObjectsWithTag("Enemy", found, 1), 1u);  // caller buffer respected
  c->SetActive(false);
  CHECK_EQ(GameObject::FindGameObjectsWithTag("Enemy", found, 4), 1u);  // inactive objects are skipped
  char longTag[64];
  memset(longTag, 'x', sizeof(longTag) - 1);
  longTag[sizeof(longTag) - 1] = 0;
  a->SetTag(longTag);
  CHECK(strlen(a->tag) == kMaxTagLength - 1);
}

TEST(objects_get_components) {
  ObjFixture f;
  GameObject* root = f.world.createObject("root");
  GameObject* kid = f.world.createObject("kid", root);
  GameObject* grand = f.world.createObject("grand", kid);
  ObjMarker* m1 = root->AddComponent<ObjMarker>();
  ObjMarker* m2 = root->AddComponent<ObjMarker>();
  ObjMarker* mg = grand->AddComponent<ObjMarker>();
  CHECK(root->GetComponent<ObjMarker>() == m1);
  ObjMarker* all[4];
  CHECK_EQ(root->GetComponents<ObjMarker>(all, 4), 2u);
  CHECK(all[0] == m1 && all[1] == m2);
  CHECK_EQ(root->GetComponents<ObjMarker>(all, 1), 1u);
  CHECK(kid->GetComponent<ObjMarker>() == nullptr);
  CHECK(kid->GetComponentInChildren<ObjMarker>() == mg);
  CHECK(root->GetComponentInChildren<ObjMarker>() == m1);  // self first
  CHECK(grand->GetComponentInParent<ObjMarker>() == mg);   // self first
  CHECK(kid->GetComponentInParent<ObjMarker>() == m1);
  CHECK(root->GetComponentInChildren<ObjOther>() == nullptr);
  grand->SetActive(false);
  CHECK(kid->GetComponentInChildren<ObjMarker>() == nullptr);
  CHECK(kid->GetComponentInChildren<ObjMarker>(true) == mg);
}

TEST(objects_transform_hierarchy) {
  ObjFixture f;
  GameObject* p = f.world.createObject("p");
  GameObject* c1 = f.world.createObject("c1", p);
  GameObject* c2 = f.world.createObject("c2", p);
  GameObject* leaf = f.world.createObject("leaf", c1);
  CHECK_EQ(p->transform.childCount(), 2u);
  CHECK(p->transform.GetChild(0) == &c1->transform);
  CHECK(p->transform.GetChild(1) == &c2->transform);
  CHECK(p->transform.GetChild(2) == nullptr);
  CHECK(p->transform.Find("c2") == &c2->transform);
  CHECK(p->transform.Find("c1/leaf") == &leaf->transform);
  CHECK(p->transform.Find("nope") == nullptr);
  CHECK(leaf->transform.parent() == &c1->transform);
  // Cannot parent under a descendant.
  p->transform.SetParent(&leaf->transform);
  CHECK(p->transform.parent() == nullptr);
}

TEST(objects_transform_spaces) {
  ObjFixture f;
  GameObject* p = f.world.createObject("p");
  GameObject* c = f.world.createObject("c", p);
  p->transform.setLocalPosition(Vector3(10, 0, 0));
  p->transform.setLocalRotation(Quaternion::Euler(0, 90, 0));
  p->transform.setLocalScale(Vector3(2, 2, 2));
  c->transform.setLocalPosition(Vector3(0, 0, 1));
  // 90 degrees about Y turns +Z into +X; scale 2 doubles the offset.
  CHECK(near(c->transform.position(), Vector3(12, 0, 0)));
  CHECK(near(c->transform.TransformPoint(Vector3(0, 0, 0)), Vector3(12, 0, 0)));
  CHECK(near(c->transform.InverseTransformPoint(Vector3(12, 0, 0)), Vector3(0, 0, 0)));
  CHECK(near(c->transform.TransformDirection(Vector3(0, 0, 1)), Vector3(1, 0, 0)));  // no scale on directions
  CHECK(near(c->transform.InverseTransformDirection(Vector3(1, 0, 0)), Vector3(0, 0, 1)));
  CHECK(near(c->transform.forward(), Vector3(1, 0, 0)));
  CHECK(near(c->transform.lossyScale(), Vector3(2, 2, 2)));
  Matrix4 w = c->transform.localToWorldMatrix() * c->transform.worldToLocalMatrix();
  CHECK(near(w.m[0], 1.0f) && near(w.m[5], 1.0f) && near(w.m[12], 0.0f));
  c->transform.setPosition(Vector3(10, 4, 0));
  CHECK(near(c->transform.localPosition(), Vector3(0, 2, 0)));  // world +4 y is local +2 under scale 2
}

TEST(objects_translate_rotate_lookat) {
  ObjFixture f;
  GameObject* go = f.world.createObject("go");
  go->transform.Rotate(Vector3(0, 90, 0));  // Space::Self
  go->transform.Translate(Vector3(0, 0, 3));  // along its own +Z, which is world +X
  CHECK(near(go->transform.position(), Vector3(3, 0, 0)));
  go->transform.Translate(Vector3(0, 1, 0), Space::World);
  CHECK(near(go->transform.position(), Vector3(3, 1, 0)));
  go->transform.setRotation(Quaternion::identity());
  go->transform.Rotate(Vector3(0, 90, 0), Space::World);
  CHECK(near(go->transform.forward(), Vector3(1, 0, 0)));
  go->transform.setPosition(Vector3(0, 0, 0));
  go->transform.LookAt(Vector3(0, 0, 10));
  CHECK(near(go->transform.forward(), Vector3(0, 0, 1)));
  go->transform.LookAt(Vector3(5, 0, 0));
  CHECK(near(go->transform.forward(), Vector3(1, 0, 0)));
  CHECK(near(go->transform.up(), Vector3(0, 1, 0)));
  go->transform.LookAt(Vector3(0, 5, 0));  // straight up: degenerate against up = +Y, still finite
  CHECK(near(go->transform.forward(), Vector3(0, 1, 0)));
}

TEST(objects_set_parent_world_position_stays) {
  ObjFixture f;
  GameObject* p = f.world.createObject("p");
  p->transform.setLocalPosition(Vector3(5, 0, 0));
  p->transform.setLocalScale(Vector3(2, 2, 2));
  GameObject* c = f.world.createObject("c");
  c->transform.setLocalPosition(Vector3(1, 1, 1));
  c->transform.SetParent(&p->transform, true);
  CHECK(near(c->transform.position(), Vector3(1, 1, 1)));
  CHECK(near(c->transform.lossyScale(), Vector3(1, 1, 1)));
  c->transform.SetParent(nullptr, false);
  c->transform.setLocalPosition(Vector3(1, 1, 1));
  c->transform.SetParent(&p->transform, false);  // keep local values
  CHECK(near(c->transform.localPosition(), Vector3(1, 1, 1)));
  CHECK(near(c->transform.position(), Vector3(7, 2, 2)));
  c->transform.SetParent(nullptr, true);
  CHECK(near(c->transform.position(), Vector3(7, 2, 2)));
  CHECK_EQ(p->transform.childCount(), 0u);
}

TEST(objects_instantiate_clones_subtree) {
  ObjFixture f;
  GameObject* src = f.world.createObject("Template");
  src->SetTag("Bullet");
  src->layer = 3;
  src->transform.setLocalScale(Vector3(2, 2, 2));
  ObjMarker* m = src->AddComponent<ObjMarker>();
  m->value = 42;
  m->speed = 9;
  MeshRenderer* mr = src->AddComponent<MeshRenderer>();
  mr->meshIndex = 5;
  mr->color = Color32(1, 2, 3, 4);
  GameObject* child = f.world.createObject("Part", src);
  child->transform.setLocalPosition(Vector3(0, 1, 0));
  child->AddComponent<ObjMarker>()->value = 8;

  GameObject* a = Instantiate(src, Vector3(4, 5, 6), Quaternion::Euler(0, 90, 0));
  CHECK(a != nullptr && a != src);
  CHECK(strcmp(a->name, "Template (Clone)") == 0);
  CHECK(a->CompareTag("Bullet") && a->layer == 3);
  CHECK(near(a->transform.position(), Vector3(4, 5, 6)));
  CHECK(near(a->transform.forward(), Vector3(1, 0, 0)));
  CHECK(near(a->transform.localScale(), Vector3(2, 2, 2)));
  ObjMarker* am = a->GetComponent<ObjMarker>();
  CHECK(am && am != m && am->value == 42 && am->speed == 9);
  CHECK(a->GetComponent<MeshRenderer>()->meshIndex == 5);  // runtime state via OnCloned
  CHECK(a->GetComponent<MeshRenderer>()->color == Color32(1, 2, 3, 4));
  CHECK_EQ(a->transform.childCount(), 1u);
  Transform* ac = a->transform.GetChild(0);
  CHECK(ac && ac->gameObject != child && strcmp(ac->gameObject->name, "Part") == 0);
  CHECK(ac->gameObject->GetComponent<ObjMarker>()->value == 8);
  CHECK(src->transform.childCount() == 1u);  // original untouched
  // Component form and parent form.
  ObjMarker* am2 = Instantiate(m);
  CHECK(am2 && am2 != m && am2->value == 42);
  GameObject* holder = f.world.createObject("holder");
  GameObject* under = Instantiate(src, &holder->transform, false);
  CHECK(under && under->transform.parent() == &holder->transform);
  // Modifying the clone does not touch the original.
  am->value = 1;
  CHECK(m->value == 42);
}

TEST(objects_instantiate_capacity_and_destroy_delay) {
  ObjFixture f(8, 4);
  CHECK(f.world.reservePool(*ObjMarker::staticTypeInfo(), 3));
  GameObject* src = f.world.createObject("Seed");
  src->AddComponent<ObjMarker>();
  GameObject* a = Instantiate(src);
  GameObject* b = Instantiate(src);
  CHECK(a && b);
  CHECK(Instantiate(src) == nullptr);  // pool of 3 is full (seed + 2 clones)
  f.frames(1);
  CHECK(f.world.objectCount() == 3u);  // the failed clone left nothing behind after the flush
  Ref<GameObject> ra = a->ref();
  Destroy(a, 0.1f);
  CHECK(ra.get() == a);  // not yet
  f.frames(3);
  CHECK(ra.get() == a);
  f.frames(10);
  CHECK(ra.get() == nullptr);
  GameObject* again = Instantiate(src);  // slot is free again
  CHECK(again != nullptr);
}

TEST(objects_destroy_component_and_children) {
  ObjFixture f;
  GameObject* p = f.world.createObject("p");
  GameObject* c = f.world.createObject("c", p);
  ObjMarker* m = c->AddComponent<ObjMarker>();
  Ref<ObjMarker> rm = f.world.ref(m);
  Ref<GameObject> rc = c->ref();
  Destroy(m);
  CHECK(rm.get() == nullptr);
  f.frames(1);
  CHECK(c->GetComponent<ObjMarker>() == nullptr);
  Destroy(p);
  CHECK(rc.get() == nullptr);  // children die with the parent
  f.frames(1);
  CHECK_EQ(f.world.objectCount(), 0u);
}

TEST(objects_debug_lines_and_assert) {
  Debug::clearLines();
  CHECK_EQ(Debug::activeLineCount(), 0u);
  Debug::DrawLine(Vector3(0, 0, 0), Vector3(1, 0, 0), Color32(255, 0, 0, 255), 0);   // this frame
  Debug::DrawLine(Vector3(0, 0, 0), Vector3(0, 1, 0), Color32(0, 255, 0, 255), 0.05f);
  CHECK_EQ(Debug::activeLineCount(), 2u);
  Debug::tickLines(1.0f / 60.0f);  // start of the next frame: the one-frame line is gone
  CHECK_EQ(Debug::activeLineCount(), 1u);
  Debug::tickLines(0.1f);
  CHECK_EQ(Debug::activeLineCount(), 0u);
  // The ring overwrites the oldest line instead of growing.
  for (u32 i = 0; i < Debug::lineCapacity() + 10; ++i) Debug::DrawLine(Vector3(0, 0, 0), Vector3(1, 1, 1), Color32(1, 1, 1, 1), 5);
  CHECK_EQ(Debug::activeLineCount(), Debug::lineCapacity());
  Debug::clearLines();
  CHECK(Debug::Assert(true, "fine"));
  CHECK(!Debug::Assert(false, "expected test assertion"));
}

TEST(objects_application) {
  Application::reset();
  CHECK(Application::isPlaying());
  CHECK(!Application::quitRequested());
  Application::setPlatform(RuntimePlatform::Headless);
  CHECK(Application::platform() == RuntimePlatform::Headless);
  Application::setTargetFrameRate(30);
  CHECK_EQ(Application::targetFrameRate(), 30);
  Application::setTargetFrameRate(0);
  CHECK_EQ(Application::targetFrameRate(), -1);
  Application::Quit();
  CHECK(Application::quitRequested());
  Application::reset();
  CHECK(!Application::quitRequested());
}
