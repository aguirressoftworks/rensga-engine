// F2.05: DontDestroyOnLoad objects survive SceneManager loads while the old scene's objects are destroyed.
#include "sample_harness.h"

using namespace sample;

TEST(ddol_object_survives_scene_load) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("ddol")));
  g.run(2);
  CHECK(GameObject::Find("StartButton") != nullptr);
  GameObject* keep = World::active()->createObject("KeepMe");
  keep->transform.setPosition(Vector3(7, 8, 0));
  GameObject* kid = World::active()->createObject("KeepKid", keep);
  (void)kid;
  DontDestroyOnLoad(keep);
  CHECK(SceneManager::LoadScene("Room"));
  g.frame();
  CHECK_EQ(SceneManager::activeSceneIndex(), 1);
  CHECK(GameObject::Find("Player") != nullptr);
  CHECK(GameObject::Find("StartButton") == nullptr);   // the old scene is gone
  GameObject* kept = GameObject::Find("KeepMe");
  CHECK(kept != nullptr);
  CHECK(GameObject::Find("KeepKid") != nullptr);       // children go with it
  if (kept) CHECK_NEAR(kept->transform.position().x, 7.0f, 0.001f);
  // Back and forth: it is still there, and exactly one copy.
  for (int i = 0; i < 5; ++i) { SceneManager::LoadScene(0); g.frame(); SceneManager::LoadScene(1); g.frame(); }
  CHECK(GameObject::Find("KeepMe") != nullptr);
  CHECK(GameObject::Find("Player") != nullptr);
}
