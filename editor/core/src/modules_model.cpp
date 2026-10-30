#include "rensga/editor/modules_model.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using namespace modules;

const char* moduleStatusWord(ModuleStatus s) {
  switch (s) {
    case ModuleStatus::Installed: return "Installed";
    case ModuleStatus::NotInstalled: return "Not installed";
    case ModuleStatus::Unpublished: return "Not published yet";
    case ModuleStatus::Manual: return "Manual";
    case ModuleStatus::NoHost: return "Not available";
    case ModuleStatus::Located: return "Located";
  }
  return "";
}

std::string formatBytes(uint64_t n) {
  char b[48];
  const double v = static_cast<double>(n);
  if (n >= 1000ull * 1000 * 1000) std::snprintf(b, sizeof b, "%.1f GB", v / 1e9);
  else if (n >= 1000ull * 1000) std::snprintf(b, sizeof b, n >= 10ull * 1000 * 1000 ? "%.0f MB" : "%.1f MB", v / 1e6);
  else if (n >= 1000) std::snprintf(b, sizeof b, n >= 10000 ? "%.0f KB" : "%.1f KB", v / 1e3);
  else std::snprintf(b, sizeof b, "%llu B", static_cast<unsigned long long>(n));
  return b;
}

ModulesModel::ModulesModel() : ModulesModel(ModuleManager(ModuleCatalog::load(ModuleCatalog::defaultDir()))) {}

ModulesModel::ModulesModel(ModuleManager manager) : mgr_(std::move(manager)) {
  if (const char* e = std::getenv("RENSGA_EDITOR_TEST_MODULE_DELAY_MS")) throttle_ = std::chrono::milliseconds(std::atoi(e));
  rebuild();
}

ModulesModel::~ModulesModel() {
  cancel_ = true;
  if (thread_.joinable()) thread_.join();
}

static uint64_t hostSize(const Module& m, const std::string& host) {
  auto it = m.hosts.find(host);
  if (it == m.hosts.end()) return 0;
  uint64_t n = 0;
  for (const ArchivePart& p : it->second.parts) n += p.size;
  return n;
}

void ModulesModel::rebuild() {
  rows_.clear();
  for (const Module& m : mgr_.catalog().all()) {
    ModuleRow r;
    r.id = m.id; r.name = m.name; r.kind = m.kind; r.target = m.target; r.version = m.version;
    r.licenseText = m.license; r.source = m.source; r.notice = m.notice; r.requires_ = m.requires_;
    for (const PostInstallStep& s : m.postInstall)
      if (!s.license.empty() && std::find(r.licenseIds.begin(), r.licenseIds.end(), s.license) == r.licenseIds.end()) r.licenseIds.push_back(s.license);
    r.size = hostSize(m, mgr_.host());
    std::string why;
    switch (mgr_.availability(m.id, &why)) {
      case Availability::Available: r.status = ModuleStatus::NotInstalled; break;
      case Availability::Unpublished: r.status = ModuleStatus::Unpublished; r.note = why; break;
      case Availability::Manual: r.status = ModuleStatus::Manual; r.note = why; break;
      default: r.status = ModuleStatus::NoHost; r.note = why; break;
    }
    if (mgr_.isInstalled(m.id)) {
      r.status = ModuleStatus::Installed;
      r.location = mgr_.root(m.id).string();
    } else if (r.status == ModuleStatus::Manual) {
      auto it = located_.find(m.id);
      if (it != located_.end() && !it->second.empty()) { r.status = ModuleStatus::Located; r.location = it->second; }
    }
    rows_.push_back(std::move(r));
  }
  // totals: what a click on Install downloads (dependencies that are not installed yet included)
  for (ModuleRow& r : rows_) {
    std::set<std::string> seen;
    std::function<void(const std::string&)> walk = [&](const std::string& id) {
      if (!seen.insert(id).second) return;
      const ModuleRow* x = find(id);
      if (!x) return;
      for (const std::string& d : x->requires_) walk(d);
      if (x->status == ModuleStatus::NotInstalled || id == r.id) r.totalSize += x->size;
    };
    walk(r.id);
  }
}

void ModulesModel::refresh() { if (!running_) rebuild(); }

const ModuleRow* ModulesModel::find(const std::string& id) const {
  for (const ModuleRow& r : rows_) if (r.id == id) return &r;
  return nullptr;
}

const ModuleRow* ModulesModel::forTarget(const std::string& targetId) const {
  if (targetId.empty()) return nullptr;
  for (const ModuleRow& r : rows_) if (r.kind == "toolchain" && r.target == targetId) return &r;
  return nullptr;
}

bool ModulesModel::missingForTarget(const std::string& targetId) const {
  const ModuleRow* r = forTarget(targetId);
  if (!r) return false;
  std::set<std::string> seen;
  std::function<bool(const ModuleRow&)> missing = [&](const ModuleRow& x) {
    if (!seen.insert(x.id).second) return false;
    if (x.status == ModuleStatus::Located) return false;
    if (x.status != ModuleStatus::Installed) return true;
    for (const std::string& d : x.requires_)
      if (const ModuleRow* dr = find(d)) if (missing(*dr)) return true;
    return false;
  };
  return missing(*r);
}

void ModulesModel::setLocated(const std::string& id, const std::string& path) {
  if (path.empty()) located_.erase(id); else located_[id] = path;
  refresh();
}

void ModulesModel::accept(const std::string& licenseId, bool yes) {
  if (yes) acceptedLicenses_.insert(licenseId); else acceptedLicenses_.erase(licenseId);
}

std::vector<std::string> ModulesModel::unacceptedLicenses(const std::string& id) const {
  std::vector<std::string> out;
  std::set<std::string> seen;
  std::function<void(const std::string&)> walk = [&](const std::string& mid) {
    if (!seen.insert(mid).second) return;
    const ModuleRow* r = find(mid);
    if (!r) return;
    for (const std::string& d : r->requires_) walk(d);
    if (r->status == ModuleStatus::Installed) return;
    for (const std::string& l : r->licenseIds)
      if (!accepted(l) && std::find(out.begin(), out.end(), l) == out.end()) out.push_back(l);
  };
  walk(id);
  return out;
}

bool ModulesModel::startInstall(const std::string& id, std::string& why) {
  if (running_) { why = "Another install is running. Wait for it or cancel it."; return false; }
  const ModuleRow* r = find(id);
  if (!r) { why = "Unknown module '" + id + "'."; return false; }
  std::string w;
  if (mgr_.availability(id, &w) != Availability::Available) { why = w; return false; }
  if (mgr_.isInstalled(id)) { why = r->name + " is already installed."; return false; }
  std::vector<std::string> need = unacceptedLicenses(id);
  if (!need.empty()) {
    why = "License '" + need[0] + "' must be accepted before installing " + r->name + ". Read it and tick I accept.";
    return false;
  }
  if (thread_.joinable()) thread_.join();
  cancel_ = false;
  finished_ = false;
  {
    std::lock_guard<std::mutex> g(mu_);
    job_ = JobSnapshot{};
    job_.state = JobState::Running;
    job_.moduleId = id;
    job_.phase = "starting";
  }
  running_ = true;
  ModuleManager worker = mgr_;                       // own copy: the UI thread keeps reading mgr_
  std::set<std::string> ok = acceptedLicenses_;
  const auto delay = throttle_;
  thread_ = std::thread([this, worker, id, ok, delay]() mutable {
    auto progress = [this, delay](const Progress& p) {
      {
        std::lock_guard<std::mutex> g(mu_);
        job_.phase = p.phase; job_.detail = p.moduleId == job_.moduleId || p.moduleId.empty() ? p.detail : p.moduleId + (p.detail.empty() ? "" : ": " + p.detail);
        if (p.phase == "download") { job_.done = p.done; job_.total = p.total; }
        else if (p.phase != "done") { job_.done = 0; job_.total = 0; }
      }
      if (delay.count() > 0 && p.phase != "done") std::this_thread::sleep_for(delay);
    };
    Result res = worker.install(id, progress, &cancel_, [ok](const std::string& lic, const Module&) { return ok.count(lic) != 0; });
    {
      std::lock_guard<std::mutex> g(mu_);
      job_.message = res.message;
      job_.state = res.ok ? JobState::Done : (cancel_.load() ? JobState::Cancelled : JobState::Failed);
    }
    finished_ = true;
    running_ = false;
  });
  return true;
}

void ModulesModel::cancel() { if (running_) cancel_ = true; }

JobSnapshot ModulesModel::job() const {
  std::lock_guard<std::mutex> g(mu_);
  return job_;
}

bool ModulesModel::pump() {
  if (!finished_.exchange(false)) return false;
  if (thread_.joinable()) thread_.join();
  rebuild();
  return true;
}

Result ModulesModel::remove(const std::string& id) {
  if (running_) return {false, "An install is running. Wait for it to finish."};
  Result r = mgr_.remove(id);
  rebuild();
  return r;
}

Result ModulesModel::verify(const std::string& id) const { return mgr_.verify(id); }

}  // namespace editor
}  // namespace rensga
