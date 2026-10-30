#include <rensga/Rensga.h>

using namespace rensga;

// API 4 exercise "coroutine door": opens over openSeconds with a coroutine (it rises by `lift`) and then fires OnOpened.
// OnOpened is a UnityEvent: wire it to DoorBell.Ring in the inspector.
struct Door : Behaviour {
  REFLECT(Door)
  RANGE(0.1f, 20)
  SERIALIZE_FIELD(Scalar, openSeconds, 2.0f)
  RANGE(0, 10)
  SERIALIZE_FIELD(Scalar, lift, 3.0f)
  SERIALIZE_FIELD(UnityEvent, OnOpened, {})

  Scalar opened = 0;  // 0..1
  Vector3 closedPos;

  void Start() override {
    closedPos = transform()->position();
    const Scalar dur = openSeconds;
    StartCoroutine([this, dur](CoroutineContext& co) -> Yield {
      if (co.step == 0) { ++co.step; return YieldNull(); }
      opened += Time::deltaTime() / dur;
      if (opened < 1) {
        transform()->setPosition(closedPos + Vector3(0, lift * opened, 0));
        return YieldNull();
      }
      opened = 1;
      transform()->setPosition(closedPos + Vector3(0, lift, 0));
      OnOpened.Invoke();
      return YieldBreak();
    });
  }
};

// The listener: Ring() counts the calls so the exercise can check that the event fired exactly once.
struct DoorBell : Behaviour {
  REFLECT(DoorBell)
  METHOD(Ring)
  METHOD(SetVolume)
  HIDE_IN_INSPECTOR()
  SERIALIZE_FIELD(i32, rings, 0)
  SERIALIZE_FIELD(Scalar, volume, 1.0f)
  void Ring() { ++rings; Debug::Log("DoorBell: ring %d at %d s", rings, static_cast<int>(Time::time())); }
  void SetVolume(Scalar v) { volume = v; }
};
