#include "rensga/pipeline/pipeline.h"

namespace rensga {
namespace pipeline {

struct Session::Impl {
  bool running = false;
};

const char* stageName(Stage s) {
  switch (s) {
    case Stage::Validate: return "validate";
    case Stage::Cook: return "cook";
    case Stage::Compile: return "compile";
    case Stage::Package: return "package";
    case Stage::Launch: return "launch";
  }
  return "?";
}
const char* statusName(Status s) {
  switch (s) {
    case Status::Started: return "started";
    case Status::Progress: return "progress";
    case Status::Succeeded: return "succeeded";
    case Status::Failed: return "failed";
    case Status::Skipped: return "skipped";
  }
  return "?";
}

Session::Session() : impl_(new Impl) {}
Session::~Session() { delete impl_; }

bool Session::run(const BuildRequest&, EventSink sink) {
  Event e;
  e.stage = Stage::Validate;
  e.status = Status::Failed;
  e.message = "not implemented";
  if (sink) sink(e);
  return false;
}
void Session::stop() {}
bool Session::running() const { return impl_->running; }
std::vector<std::string> Session::drainGameLog() { return {}; }

}  // namespace pipeline
}  // namespace rensga
