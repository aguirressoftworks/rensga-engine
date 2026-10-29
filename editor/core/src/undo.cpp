#include "rensga/editor/undo.h"

namespace rensga {
namespace editor {

const std::string& Command::mergeKey() const {
  static const std::string none;
  return none;
}

namespace {
// Several commands as one step: undo in reverse order, redo in order.
class CompositeCommand : public Command {
 public:
  CompositeCommand(std::string n, std::vector<std::unique_ptr<Command>> c) : name_(std::move(n)), cmds_(std::move(c)) {}
  const char* name() const override { return name_.c_str(); }
  void undo() override {
    for (size_t i = cmds_.size(); i-- > 0;) cmds_[i]->undo();
  }
  void redo() override {
    for (auto& c : cmds_) c->redo();
  }

 private:
  std::string name_;
  std::vector<std::unique_ptr<Command>> cmds_;
};
}  // namespace

void UndoStack::push(std::unique_ptr<Command> c) {
  // A new edit invalidates the redo branch; a clean position inside it can never be reached again.
  if (!redo_.empty()) {
    if (cleanPos_ > undo_.size()) cleanValid_ = false;
    redo_.clear();
  }
  auto now = std::chrono::steady_clock::now();
  if (lastMerge_ && !undo_.empty() && cleanPos_ != undo_.size() && !c->mergeKey().empty() &&
      c->mergeKey() == undo_.back()->mergeKey() &&
      std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime_).count() <= mergeWindowMs_ &&
      undo_.back()->mergeWith(*c)) {
    lastTime_ = now;
    return;
  }
  undo_.push_back(std::move(c));
  lastMerge_ = !undo_.back()->mergeKey().empty();
  lastTime_ = now;
  if (undo_.size() > limit_) {
    undo_.erase(undo_.begin());
    if (cleanPos_ == 0) cleanValid_ = false;
    else --cleanPos_;
  }
}

void UndoStack::record(std::unique_ptr<Command> c) {
  if (!c) return;
  if (groupDepth_ > 0) {
    if (!group_.empty() && c->groupMerge() && group_.back()->groupMerge() && group_.back()->mergeWith(*c)) return;
    group_.push_back(std::move(c));
    return;
  }
  push(std::move(c));
}

void UndoStack::execute(std::unique_ptr<Command> c) {
  if (!c) return;
  c->redo();
  record(std::move(c));
}

void UndoStack::beginGroup(const char* name) {
  if (groupDepth_++ == 0) {
    group_.clear();
    groupName_ = name ? name : "group";
  }
}

void UndoStack::endGroup() {
  if (groupDepth_ == 0 || --groupDepth_ > 0) return;
  if (group_.empty()) return;
  lastMerge_ = false;  // a group is a finished step
  if (group_.size() == 1) push(std::move(group_[0]));
  else push(std::make_unique<CompositeCommand>(groupName_, std::move(group_)));
  lastMerge_ = false;
  group_.clear();
}

bool UndoStack::undo() {
  if (undo_.empty()) return false;
  undo_.back()->undo();
  redo_.push_back(std::move(undo_.back()));
  undo_.pop_back();
  lastMerge_ = false;
  return true;
}

bool UndoStack::redo() {
  if (redo_.empty()) return false;
  redo_.back()->redo();
  undo_.push_back(std::move(redo_.back()));
  redo_.pop_back();
  lastMerge_ = false;
  return true;
}

void UndoStack::clear() {
  undo_.clear();
  redo_.clear();
  group_.clear();
  groupDepth_ = 0;
  lastMerge_ = false;
  cleanPos_ = 0;
  cleanValid_ = true;
}

void UndoStack::markClean() {
  cleanPos_ = undo_.size();
  cleanValid_ = true;
  lastMerge_ = false;  // never merge into a saved state
}

}  // namespace editor
}  // namespace rensga
