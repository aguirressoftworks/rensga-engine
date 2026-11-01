#include <rensga/Rensga.h>
#include <stdio.h>

using namespace rensga;

// F6.06 frame_spike: a deliberate frame spike for the Profiler window.
//   spread = false (scene Spiky):  every `period` frames the planner runs one whole job (`work` iterations) inside a single Update.
//                                  That frame takes many times the budget; the other frames are almost empty.
//   spread = true  (scene Smooth): the same job is cut into `sliceFrames` slices, one per frame. Same total work, no spike.
// The job is wrapped in Profiler::BeginSample("PlanRoute"); with a development build the runtime streams it to the editor's Profiler
// window ("rensga: pf ..." lines), which is how the spike is found: the timeline shows a red-topped bar every 120 frames and the
// sample tree of the selected frame names PlanRoute. The script logs a summary and quits after runFrames:
//   rensga: spike: mode=spiky|smooth frames=<n> jobs=<n> slice=<iterations per Update> check=<integer checksum>
struct RoutePlanner : Behaviour {
  REFLECT(RoutePlanner)
  TOOLTIP("true: spread each job over sliceFrames frames; false: run it all in one frame.")
  SERIALIZE_FIELD(bool, spread, false)
  TOOLTIP("Frames between two jobs.")
  SERIALIZE_FIELD(i32, period, 120)
  TOOLTIP("Loop iterations of one job.")
  SERIALIZE_FIELD(i32, work, 1500000)
  TOOLTIP("Frames one job is spread over when spread is on.")
  SERIALIZE_FIELD(i32, sliceFrames, 40)
  TOOLTIP("Frames before the run ends (the script quits the game).")
  SERIALIZE_FIELD(i32, runFrames, 730)

  i32 frame = 0, jobs = 0, done = 0;
  bool running = false;
  Scalar acc = 0;
  i32 check = 0;

  void Update() override {
    ++frame;
    if (!running && period > 0 && frame % period == 0) {
      running = true;
      done = 0;
      acc = 0;
    }
    if (running) {
      Profiler::BeginSample("PlanRoute");
      const i32 slice = spread && sliceFrames > 0 ? work / sliceFrames : work;
      i32 end = done + slice;
      if (end > work) end = work;
      for (; done < end; ++done) acc = acc * 0.999f + static_cast<Scalar>(done & 255) * 0.01f;  // deterministic busy work
      Profiler::EndSample();
      if (done >= work) {
        running = false;
        ++jobs;
        check += static_cast<i32>(acc);
      }
    }
    if (frame >= runFrames) {
      Debug::Log("rensga: spike: mode=%s frames=%d jobs=%d slice=%d check=%d", spread ? "smooth" : "spiky", frame, jobs,
                 spread && sliceFrames > 0 ? work / sliceFrames : work, check);
      Application::Quit();
    }
  }
};

// Something to look at while the planner works.
struct Spinner : Behaviour {
  REFLECT(Spinner)
  SERIALIZE_FIELD(i32, degreesPerSecond, 60)
  i32 frame = 0;
  void Update() override {
    ++frame;
    transform()->setRotation(Quaternion::Euler(0, static_cast<Scalar>(frame * degreesPerSecond) / 60.0f, 0));
  }
};
