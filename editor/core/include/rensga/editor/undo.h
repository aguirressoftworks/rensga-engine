// F1.05: command-based undo for every editing path. Commands are applied by their owner first; the stack only
// records them (record) or applies and records (execute). Features: merge of consecutive edits with the same key
// (a drag), groups (several commands = one step), >= 256 steps, dirty tracking against the saved state.
// No ImGui, no console names.
#pragma once
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rensga {
namespace editor {

class Command {
 public:
  virtual ~Command() = default;
  virtual const char* name() const = 0;
  virtual void undo() = 0;
  virtual void redo() = 0;
  // Non-empty: this command may absorb a following command with the same key (see UndoStack::mergeWindowMs).
  virtual const std::string& mergeKey() const;
  // Absorbs `next` (applied after this one) and returns true; the stack then drops `next`.
  virtual bool mergeWith(const Command& next) { (void)next; return false; }
  // Inside a group, commands also merge when `groupMerge()` is true (whole-state snapshots).
  virtual bool groupMerge() const { return false; }
};

class UndoStack {
 public:
  static constexpr size_t kDefaultLimit = 256;

  explicit UndoStack(size_t limit = kDefaultLimit) : limit_(limit) {}

  // Records a command whose effect is already applied. Clears the redo branch.
  void record(std::unique_ptr<Command> c);
  // Applies (redo) and records.
  void execute(std::unique_ptr<Command> c);

  // Groups: everything recorded between begin and end is one undo step. Groups nest; only the outermost counts.
  void beginGroup(const char* name = "group");
  void endGroup();
  bool inGroup() const { return groupDepth_ > 0; }

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  size_t depth() const { return undo_.size(); }
  size_t redoDepth() const { return redo_.size(); }
  const char* undoName() const { return undo_.empty() ? "" : undo_.back()->name(); }
  const char* redoName() const { return redo_.empty() ? "" : redo_.back()->name(); }
  bool undo();
  bool redo();
  void clear();
  size_t limit() const { return limit_; }

  // Merge window: a command merges into the previous one only if recorded within this many ms (0 = never merge
  // outside groups). Tests may set it huge.
  void setMergeWindowMs(long ms) { mergeWindowMs_ = ms; }
  // Ends the current merge run (call on mouse release, focus change...).
  void breakMerge() { lastMerge_ = false; }

  // Dirty tracking: markClean() remembers the current position (the saved state).
  void markClean();
  bool dirty() const { return cleanPos_ != undo_.size() || !cleanValid_; }

 private:
  void push(std::unique_ptr<Command> c);

  std::vector<std::unique_ptr<Command>> undo_, redo_;
  std::vector<std::unique_ptr<Command>> group_;
  std::string groupName_;
  int groupDepth_ = 0;
  size_t limit_;
  long mergeWindowMs_ = 600;
  bool lastMerge_ = false;  // the top command may still absorb merges
  std::chrono::steady_clock::time_point lastTime_{};
  size_t cleanPos_ = 0;
  bool cleanValid_ = true;
};

}  // namespace editor
}  // namespace rensga
