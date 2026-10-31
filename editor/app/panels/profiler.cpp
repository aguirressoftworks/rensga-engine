// F5.10: Profiler panel, fed by the game's log lines ("rensga: prof ...", "rensga: gu draws=...", "[gu] vram tex=...", "rensga: mem
// budget=...", "rensga: ... clock ... cpu=...") that Play on console routes into the Console, and by the editor's own numbers
// during Play in editor. Also the PPSSPP debugger button: the emulator's WebSocket debugger (ws://127.0.0.1:<port>/debugger) when it
// runs with RemoteDebuggerOnStartup, else a relaunch with the debugger flag.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dock_debug.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <set>
#include <sstream>
#endif

namespace rensga {
namespace editor {

namespace {

struct Sample {
  float total = 0, totalMax = 0, update = 0, render = 0, present = 0, audio = 0;  // ms
};

struct Prof {
  std::vector<Sample> ring;          // newest last
  size_t cursor = 0;                 // EditorLog lines already read
  bool fromConsole = false, fromEditor = false;
  long frames = 0;                   // prof lines parsed
  unsigned draws = 0, tris = 0, lights = 0, batches = 0;
  bool haveGu = false;
  unsigned vramUsed = 0, vramArea = 0, ramTex = 0;
  unsigned drawsBudget = 0, texUsed = 0, texBudget = 0;  // from 'rensga: budget scene=<s> draws=a/b tris=a/b textures=a/b vram=a/b'
  bool haveBudgetLine = false;
  std::string scene;
  unsigned memBudget = 0, memUsed = 0, memPeak = 0;
  int cpuMhz = 0;
  uint64_t trisBudget = 0;
  float frameBudgetMs = 1000.0f / 60.0f;
  int editorTick = 0;
  // PPSSPP debugger
  int dbgPort = 0;
  std::string dbgText;
  int dbgOk = 0;                     // -1 failed, 0 unknown, 1 connected
};
Prof g_p;

bool numAfter(const std::string& s, const char* key, unsigned& v) {
  const size_t k = s.find(key);
  if (k == std::string::npos) return false;
  v = static_cast<unsigned>(std::strtoul(s.c_str() + k + std::strlen(key), nullptr, 10));
  return true;
}

// "key=avg/max" -> avg, max in microseconds
bool pair(const std::string& s, const char* key, float& avg, float& mx) {
  const size_t k = s.find(key);
  if (k == std::string::npos) return false;
  unsigned a = 0, m = 0;
  if (std::sscanf(s.c_str() + k + std::strlen(key), "%u/%u", &a, &m) < 1) return false;
  avg = a / 1000.0f;
  mx = m / 1000.0f;
  return true;
}

void push(const Sample& s) {
  g_p.ring.push_back(s);
  if (g_p.ring.size() > 240) g_p.ring.erase(g_p.ring.begin());
}

// One log line of the game. Returns true when it was a profiler line.
bool feed(const std::string& l) {
  if (l.find("rensga: prof ") != std::string::npos) {
    Sample s;
    float mx = 0, d = 0;
    pair(l, "total_us=", s.total, s.totalMax);
    pair(l, "update_us=", s.update, mx);
    pair(l, "render_us=", s.render, mx);
    pair(l, "present_us=", s.present, mx);
    pair(l, "audio_us=", s.audio, d);
    push(s);
    ++g_p.frames;
    g_p.fromConsole = true;
    return true;
  }
  if (const size_t k = l.find("rensga: budget scene="); k != std::string::npos) {
    unsigned d = 0, db = 0, t = 0, tb = 0, x = 0, xb = 0, v = 0, vb = 0;
    const size_t e = l.find(' ', k + 21);
    if (e != std::string::npos && std::sscanf(l.c_str() + e, " draws=%u/%u tris=%u/%u textures=%u/%u vram=%u/%u", &d, &db, &t, &tb, &x, &xb, &v, &vb) == 8) {
      g_p.scene = l.substr(k + 21, e - k - 21);
      g_p.draws = d; g_p.drawsBudget = db; g_p.tris = t; g_p.trisBudget = tb;
      g_p.texUsed = x; g_p.texBudget = xb; g_p.vramUsed = v; g_p.vramArea = vb;
      g_p.haveBudgetLine = g_p.haveGu = true;
      g_p.fromConsole = true;
      return true;
    }
  }
  if (l.find("rensga: gu draws=") != std::string::npos) {
    unsigned v;
    if (!g_p.haveBudgetLine && numAfter(l, "draws=", v)) g_p.draws = v;
    if (!g_p.haveBudgetLine && numAfter(l, " tris=", v)) g_p.tris = v;
    if (numAfter(l, "lights=", v)) g_p.lights = v;
    if (numAfter(l, "batches=", v)) g_p.batches = v;
    g_p.haveGu = true;
    g_p.fromConsole = true;
    return true;
  }
  if (l.find("vram tex=") != std::string::npos) {
    unsigned used = 0, area = 0, ram = 0;
    const size_t k = l.find("vram tex=");
    if (std::sscanf(l.c_str() + k, "vram tex=%u/%u ram_tex=%u", &used, &area, &ram) >= 2) {
      g_p.vramUsed = used;
      g_p.vramArea = area;
      g_p.ramTex = ram;
      return true;
    }
  }
  if (l.find("rensga: mem budget=") != std::string::npos) {
    unsigned v;
    if (numAfter(l, "budget=", v)) g_p.memBudget = v;
    if (numAfter(l, " used=", v)) g_p.memUsed = v;
    if (numAfter(l, " peak=", v)) g_p.memPeak = v;
    return true;
  }
  if (l.find("rensga:") != std::string::npos && l.find("clock") != std::string::npos) {
    const size_t k = l.find(" cpu=");
    if (k != std::string::npos) { g_p.cpuMhz = std::atoi(l.c_str() + k + 5); return true; }
  }
  return false;
}

void pumpLog(App& a) {
  EditorLog& log = EditorLog::instance();
  const size_t n = log.size();
  if (n < g_p.cursor) g_p = Prof();           // Clear in the Console: start over
  if (n == g_p.cursor) return;
  const std::vector<LogLine> lines = log.snapshot();
  for (size_t i = g_p.cursor; i < lines.size(); ++i)
    if (lines[i].source != LogSource::Editor) feed(lines[i].text);
  g_p.cursor = lines.size();
  (void)a;
}

void pumpEditor(App& a) {
  if (!a.session.active() || a.session.state() != PlayState::Playing) return;
  if (++g_p.editorTick % 6) return;
  const PlayStats& st = a.session.stats();
  Sample s;
  s.update = static_cast<float>(st.simMs);
  s.total = st.fps > 0 ? static_cast<float>(1000.0 / st.fps) : s.update;
  s.totalMax = s.total;
  push(s);
  g_p.fromEditor = true;
  g_p.draws = st.draws;
  g_p.tris = st.triangles;
  g_p.haveGu = true;
  g_p.trisBudget = st.trianglesBudget;
  g_p.drawsBudget = g_p.texBudget = 0;
  g_p.vramUsed = static_cast<unsigned>(st.vramUsed);
  g_p.vramArea = static_cast<unsigned>(st.vramCapacity);
  g_p.memUsed = static_cast<unsigned>(st.memUsed);
  g_p.memBudget = static_cast<unsigned>(st.memCapacity);
}

// ---- PPSSPP debugger (WebSocket) ---------------------------------------------------------------------------------------

#if !defined(_WIN32)
// Listening TCP ports owned by a descendant process of the editor (the emulator).
std::vector<int> emulatorPorts() {
  std::set<long> pids;
  std::set<std::pair<long, long>> links;
  if (DIR* d = opendir("/proc")) {
    while (dirent* e = readdir(d)) {
      char* end = nullptr;
      const long pid = std::strtol(e->d_name, &end, 10);
      if (!end || *end || pid <= 0) continue;
      std::ifstream f(std::string("/proc/") + e->d_name + "/stat");
      std::string s;
      std::getline(f, s);
      const size_t rp = s.rfind(')');
      char st;
      long pp = 0;
      if (rp != std::string::npos && std::sscanf(s.c_str() + rp + 1, " %c %ld", &st, &pp) == 2) links.insert({pid, pp});
    }
    closedir(d);
  }
  std::set<long> frontier{static_cast<long>(getpid())};
  while (!frontier.empty()) {
    std::set<long> next;
    for (const auto& l : links)
      if (frontier.count(l.second) && !pids.count(l.first)) { pids.insert(l.first); next.insert(l.first); }
    frontier = next;
  }
  std::set<std::string> inodes;
  for (long pid : pids) {
    const std::string dir = "/proc/" + std::to_string(pid) + "/fd";
    if (DIR* d = opendir(dir.c_str())) {
      while (dirent* e = readdir(d)) {
        char buf[128];
        const ssize_t n = readlink((dir + "/" + e->d_name).c_str(), buf, sizeof buf - 1);
        if (n > 0) {
          buf[n] = 0;
          if (!std::strncmp(buf, "socket:[", 8)) inodes.insert(std::string(buf + 8, std::strlen(buf + 8) - 1));
        }
      }
      closedir(d);
    }
  }
  std::vector<int> ports;
  for (const char* table : {"/proc/net/tcp", "/proc/net/tcp6"}) {  // PPSSPP listens on a dual-stack wildcard socket (tcp6)
    std::ifstream t(table);
    std::string line;
    std::getline(t, line);
    while (std::getline(t, line)) {
      std::istringstream is(line);
      std::string f[10];
      for (auto& x : f) is >> x;
      if (f[3] != "0A") continue;  // LISTEN
      const size_t c = f[1].rfind(':');
      if (c == std::string::npos) continue;
      if (inodes.count(f[9])) ports.push_back(static_cast<int>(std::strtol(f[1].c_str() + c + 1, nullptr, 16)));
    }
  }
  return ports;
}

int connectLocal(int port, int timeoutMs) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
  timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  if (connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0) { close(fd); return -1; }
  return fd;
}

bool readN(int fd, unsigned char* p, size_t n) {
  while (n) {
    const ssize_t r = recv(fd, p, n, 0);
    if (r <= 0) return false;
    p += r;
    n -= static_cast<size_t>(r);
  }
  return true;
}

// Minimal RFC 6455 client: handshake on /debugger, one masked text frame out, one frame in.
bool wsRequest(int port, const std::string& request, std::string& reply) {
  const int fd = connectLocal(port, 700);
  if (fd < 0) return false;
  struct Closer { int fd; ~Closer() { close(fd); } } closer{fd};
  const std::string hs = "GET /debugger HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                         "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: cmVuc2dhLWVkaXRvci0xNg==\r\nSec-WebSocket-Version: 13\r\n\r\n";
  if (send(fd, hs.data(), hs.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(hs.size())) return false;
  std::string head;
  char c;
  while (head.size() < 4096 && recv(fd, &c, 1, 0) == 1) {
    head.push_back(c);
    if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0) break;
  }
  if (head.find(" 101 ") == std::string::npos) return false;
  std::string frame;
  frame.push_back(static_cast<char>(0x81));
  const unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
  if (request.size() < 126) {
    frame.push_back(static_cast<char>(0x80 | request.size()));
  } else {
    frame.push_back(static_cast<char>(0x80 | 126));
    frame.push_back(static_cast<char>(request.size() >> 8));
    frame.push_back(static_cast<char>(request.size() & 255));
  }
  frame.append(reinterpret_cast<const char*>(mask), 4);
  for (size_t i = 0; i < request.size(); ++i) frame.push_back(static_cast<char>(request[i] ^ mask[i % 4]));
  if (send(fd, frame.data(), frame.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(frame.size())) return false;
  for (int tries = 0; tries < 200; ++tries) {  // PPSSPP interleaves log events with the answer
    unsigned char h[2];
    if (!readN(fd, h, 2)) return false;
    uint64_t len = h[1] & 0x7f;
    if (len == 126) { unsigned char e[2]; if (!readN(fd, e, 2)) return false; len = (e[0] << 8) | e[1]; }
    else if (len == 127) return false;
    std::string payload(len, '\0');
    if (len && !readN(fd, reinterpret_cast<unsigned char*>(&payload[0]), len)) return false;
    if ((h[0] & 0x0f) == 1 && payload.find("\"event\":\"cpu.status\"") != std::string::npos) { reply = payload; return true; }
    if ((h[0] & 0x0f) == 1 && payload.find("\"event\":\"error\"") != std::string::npos) { reply = payload; return false; }
  }
  return false;
}
#endif

// Probes the emulator's debugger. True and a one-line summary when the WebSocket answers cpu.status.
bool probeDebugger(std::string& text, int& port) {
#if defined(_WIN32)
  text = "debugger probe is not implemented on Windows yet";
  port = 0;
  return false;
#else
  for (int p : emulatorPorts()) {
    std::string reply;
    if (wsRequest(p, "{\"event\":\"cpu.status\"}", reply)) {
      port = p;
      auto field = [&](const char* k) {
        const size_t i = reply.find(k);
        if (i == std::string::npos) return std::string("?");
        size_t b = i + std::strlen(k), e = b;
        while (e < reply.size() && reply[e] != ',' && reply[e] != '}') ++e;
        return reply.substr(b, e - b);
      };
      text = "cpu.status stepping=" + field("\"stepping\":") + " pc=" + field("\"pc\":") + " (ws://127.0.0.1:" + std::to_string(p) + "/debugger)";
      return true;
    }
  }
  port = 0;
  text = "no debugger answered on the emulator's ports";
  return false;
#endif
}

void openUrl(const std::string& url) {
  if (const char* no = std::getenv("RENSGA_NO_OPEN"); no && *no) return;  // tests
#if defined(_WIN32)
  std::system(("start \"\" \"" + url + "\"").c_str());
#else
  std::system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str());
#endif
}

// The button: debugger already on -> probe + open the web debugger; else relaunch with the debugger flag.
std::string openDebugger(App& a) {
  if (a.play.status() != PlayStatus::Running) {
    if (a.play.relaunchWithDebugger()) return "no run: started Play with the debugger on";
    return "start Play on console first";
  }
  std::string text;
  int port = 0;
  if (probeDebugger(text, port)) {
    g_p.dbgOk = 1;
    g_p.dbgPort = port;
    g_p.dbgText = text;
    openUrl("http://127.0.0.1:" + std::to_string(port) + "/debugger");
    return "connected: " + text;
  }
  g_p.dbgOk = -1;
  if (a.play.relaunchWithDebugger()) {
    g_p.dbgText = "debugger was off: relaunching the emulator with it on";
    return g_p.dbgText;
  }
  g_p.dbgText = "cannot relaunch";
  return g_p.dbgText;
}

// ---- drawing -----------------------------------------------------------------------------------------------------------

const char* levelWord(double used, double budget) {
  if (budget <= 0) return "no budget";
  const double r = used / budget;
  return r > 1.02 ? "over" : r > 0.8 ? "near" : "ok";  // 2%: a vsync-locked frame sits exactly on the budget
}
ImVec4 levelColor(double used, double budget) {
  if (budget <= 0) return brandToken("muted");
  const double r = used / budget;
  return brandToken(r > 1.02 ? "danger" : r > 0.8 ? "warn" : "ok");
}

void budgetRow(const char* name, const char* value, double used, double budget, const char* budgetText) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
  ImGui::TableNextColumn(); ImGui::TextUnformatted(value);
  ImGui::TableNextColumn(); ImGui::TextColored(brandToken("text-2"), "%s", budgetText);
  ImGui::TableNextColumn(); ImGui::TextColored(levelColor(used, budget), "%s", levelWord(used, budget));
}

void drawGraph(const ImVec2 size) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##graph", size);
  dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), ImGui::GetColorU32(brandToken("surface-2")));
  float top = g_p.frameBudgetMs * 1.5f;
  for (const Sample& s : g_p.ring) top = std::max(top, std::max(s.total, s.update + s.render + s.present) * 1.1f);
  const size_t n = g_p.ring.size();
  const float barW = std::max(2.0f, size.x / 240.0f);
  const auto y = [&](float ms) { return p0.y + size.y - std::min(ms, top) / top * size.y; };
  for (size_t i = 0; i < n; ++i) {
    const Sample& s = g_p.ring[i];
    const float x1 = p0.x + size.x - (n - i - 1) * barW, x0 = x1 - barW + 1;
    if (x0 < p0.x) continue;
    float acc = 0;
    const struct { float v; const char* tok; } parts[3] = {{s.update, "info"}, {s.render, "primary"}, {s.present, "text-2"}};
    for (const auto& pr : parts) {
      dl->AddRectFilled(ImVec2(x0, y(acc + pr.v)), ImVec2(x1, y(acc)), ImGui::GetColorU32(brandToken(pr.tok)));
      acc += pr.v;
    }
    if (acc < s.total * 0.98f) dl->AddRectFilled(ImVec2(x0, y(s.total)), ImVec2(x1, y(acc)), ImGui::GetColorU32(brandToken("border-strong")));
  }
  const float by = y(g_p.frameBudgetMs);
  dl->AddLine(ImVec2(p0.x, by), ImVec2(p0.x + size.x, by), ImGui::GetColorU32(brandToken("danger")), 1.5f);
  char b[48];
  std::snprintf(b, sizeof b, "budget %.1f ms", g_p.frameBudgetMs);
  dl->AddText(ImVec2(p0.x + 6, by - ImGui::GetTextLineHeight() - 1), ImGui::GetColorU32(brandToken("danger")), b);
  std::snprintf(b, sizeof b, "%.0f ms", top);
  dl->AddText(ImVec2(p0.x + 6, p0.y + 2), ImGui::GetColorU32(brandToken("muted")), b);
  if (!n) {
    const char* m = "Waiting for 'rensga: prof' lines (Play on console) or Play in editor.";
    const ImVec2 ts = ImGui::CalcTextSize(m);
    dl->AddText(ImVec2(p0.x + (size.x - ts.x) * 0.5f, p0.y + (size.y - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("muted")), m);
  }
}

void legend(const char* tok, const char* name) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = ImGui::GetTextLineHeight();
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 3), ImVec2(p.x + h - 4, p.y + h - 1), ImGui::GetColorU32(brandToken(tok)));
  ImGui::Dummy(ImVec2(h, h));
  ImGui::SameLine(0, 0);
  ImGui::TextUnformatted(name);
  ImGui::SameLine(0, 14);
}

Sample average() {
  Sample s;
  if (g_p.ring.empty()) return s;
  const size_t n = std::min<size_t>(g_p.ring.size(), 10);
  for (size_t i = g_p.ring.size() - n; i < g_p.ring.size(); ++i) {
    const Sample& r = g_p.ring[i];
    s.total += r.total; s.update += r.update; s.render += r.render; s.present += r.present; s.audio += r.audio;
    s.totalMax = std::max(s.totalMax, r.totalMax);
  }
  s.total /= n; s.update /= n; s.render /= n; s.present /= n; s.audio /= n;
  return s;
}

}  // namespace

void drawProfiler(App& a) {
  if (!a.loaded) return;
  pumpLog(a);
  pumpEditor(a);
  const ConsoleTargetData& ct = consoleTargetFor(a.project.root);
  g_p.frameBudgetMs = 1000.0f / static_cast<float>(ct.refreshHz ? ct.refreshHz : 60);
  if (!g_p.trisBudget) g_p.trisBudget = ct.trianglesPerFrame;
  DockDebugState& st = dockDebug();
  if (!st.showProfiler) return;
  if (!ImGui::Begin("Profiler", &st.showProfiler)) { ImGui::End(); return; }

  const bool console = a.play.status() == PlayStatus::Running || (g_p.fromConsole && !a.session.active());
  ImGui::TextColored(brandToken("text-2"), "Source: %s%s%s", a.session.active() ? "Play in editor" : console ? "game log (Play on console)" : "idle", g_p.scene.empty() || a.session.active() ? "" : "   scene ", a.session.active() ? "" : g_p.scene.c_str());
  ImGui::SameLine();
  if (g_p.cpuMhz) ImGui::TextColored(brandToken("text-2"), "   CPU %d MHz", g_p.cpuMhz);
  ImGui::SameLine(ImGui::GetContentRegionMax().x - 300);
  if (ImGui::Button("Open PPSSPP debugger")) {
    const std::string r = openDebugger(a);
    EditorLog::instance().info("profiler: " + r);
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) { const size_t c = g_p.cursor; g_p = Prof(); g_p.cursor = c; }

  // Wide panel: graph on the left, budget table on the right. Narrow: stacked.
  const bool wide = ImGui::GetContentRegionAvail().x > 760;
  const float gw = wide ? ImGui::GetContentRegionAvail().x * 0.46f : ImGui::GetContentRegionAvail().x;
  const float gh = wide ? std::max(90.0f, ImGui::GetContentRegionAvail().y - 52) : std::max(90.0f, std::min(160.0f, ImGui::GetContentRegionAvail().y * 0.45f));
  ImGui::BeginGroup();
  drawGraph(ImVec2(gw, gh));
  legend("info", "update");
  legend("primary", "render");
  legend("text-2", "present");
  legend("border-strong", "other");
  ImGui::NewLine();
  ImGui::EndGroup();
  if (wide) ImGui::SameLine();

  const Sample s = average();
  char v[96], bt[96];
  ImGui::BeginGroup();
  if (ImGui::BeginTable("##prof", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_None, 1.3f);
    ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_None, 1.6f);
    ImGui::TableSetupColumn("Budget", ImGuiTableColumnFlags_None, 1.2f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None, 0.7f);
    ImGui::TableHeadersRow();
    std::snprintf(v, sizeof v, "%.2f ms avg  (max %.2f)", s.total, s.totalMax);
    std::snprintf(bt, sizeof bt, "%.2f ms (%u Hz)", g_p.frameBudgetMs, ct.refreshHz ? ct.refreshHz : 60);
    budgetRow("Frame time", v, s.total, g_p.ring.empty() ? 0 : g_p.frameBudgetMs, bt);
    std::snprintf(v, sizeof v, "u %.2f  r %.2f  p %.2f ms", s.update, s.render, s.present);
    budgetRow("Split u / r / p", v, 0, 0, "");
    std::snprintf(v, sizeof v, "%u", g_p.draws);
    if (g_p.drawsBudget) std::snprintf(bt, sizeof bt, "%u / frame", g_p.drawsBudget); else std::snprintf(bt, sizeof bt, "no limit set");
    budgetRow("Draw calls", g_p.haveGu ? v : "-", g_p.draws, g_p.drawsBudget, bt);
    if (g_p.texBudget) {
      std::snprintf(v, sizeof v, "%u", g_p.texUsed);
      std::snprintf(bt, sizeof bt, "%u textures", g_p.texBudget);
      budgetRow("Textures", v, g_p.texUsed, g_p.texBudget, bt);
    }
    std::snprintf(v, sizeof v, "%u", g_p.tris);
    if (g_p.trisBudget) std::snprintf(bt, sizeof bt, "%llu / frame", static_cast<unsigned long long>(g_p.trisBudget)); else std::snprintf(bt, sizeof bt, "no limit set");
    budgetRow("Triangles", g_p.haveGu ? v : "-", g_p.tris, static_cast<double>(g_p.trisBudget), bt);
    std::snprintf(v, sizeof v, "%.1f / %.1f KiB", g_p.vramUsed / 1024.0, g_p.vramArea / 1024.0);
    std::snprintf(bt, sizeof bt, "%.0f KiB", g_p.vramArea / 1024.0);
    budgetRow("Texture VRAM", g_p.vramArea ? v : "-", g_p.vramUsed, g_p.vramArea, bt);
    std::snprintf(v, sizeof v, "%.2f MiB (peak %.2f)", g_p.memUsed / 1048576.0, g_p.memPeak / 1048576.0);
    std::snprintf(bt, sizeof bt, "%.1f MiB", ct.ramBytes / 1048576.0);
    budgetRow("RAM", g_p.memBudget ? v : "-", g_p.memUsed, g_p.memBudget, bt);
    ImGui::EndTable();
  }
  ImGui::EndGroup();
  if (!g_p.dbgText.empty()) {
    ImGui::TextColored(brandToken(g_p.dbgOk > 0 ? "ok" : "warn"), "%s", g_p.dbgOk > 0 ? "Debugger: connected" : "Debugger: not connected");
    ImGui::SameLine();
    ImGui::TextColored(brandToken("text-2"), "%s", g_p.dbgText.c_str());
  }
  ImGui::End();
}

namespace {
struct DbgWait {
  std::string event;
  std::chrono::steady_clock::time_point start, lastProbe;
  bool active = false, ok = false, timedOut = false;
};
DbgWait g_dw;
}  // namespace

// profilerwait:<seconds>: holds the --ui-event queue until the emulator's debugger answers cpu.status (a test waits for the relaunched
// emulator this way). The probe runs every 300 ms; the UI keeps drawing.
bool profilerUiHolds(App&, const std::string& event) {
  if (event.rfind("profilerwait", 0) != 0) return false;
  const auto now = std::chrono::steady_clock::now();
  if (!g_dw.active || g_dw.event != event) {
    g_dw = DbgWait();
    g_dw.active = true;
    g_dw.event = event;
    g_dw.start = g_dw.lastProbe = now - std::chrono::seconds(1);
  }
  if (g_dw.ok) return false;
  if (now - g_dw.start > std::chrono::seconds(event.size() > 13 ? std::atoi(event.c_str() + 13) : 60)) { g_dw.timedOut = true; return false; }
  if (now - g_dw.lastProbe > std::chrono::milliseconds(300)) {
    g_dw.lastProbe = now;
    std::string t;
    int port = 0;
    if (probeDebugger(t, port)) { g_dw.ok = true; g_p.dbgOk = 1; g_p.dbgPort = port; g_p.dbgText = t; return false; }
  }
  return true;
}

bool profilerUiHook(App& a, const std::string& act, const std::string& arg) {
  if (act == "profiler") { ImGui::SetWindowFocus("Profiler"); dockDebug().showProfiler = true; return true; }
  if (act == "profilerclear") { const size_t c = g_p.cursor; g_p = Prof(); g_p.cursor = c; return true; }
  if (act == "profilerfeed") { std::printf("profiler: feed %s\n", feed(arg) ? "parsed" : "ignored"); return true; }
  if (act == "profilerdump") {
    const Sample s = average();
    std::printf("profiler: samples=%zu prof_lines=%ld total_ms=%.3f/%.3f update_ms=%.3f render_ms=%.3f present_ms=%.3f draws=%u tris=%u vram=%u/%u mem=%u/%u cpu_mhz=%d source=%s\n",
                g_p.ring.size(), g_p.frames, s.total, s.totalMax, s.update, s.render, s.present, g_p.draws, g_p.tris, g_p.vramUsed, g_p.vramArea, g_p.memUsed,
                g_p.memBudget, g_p.cpuMhz, a.session.active() ? "editor" : g_p.fromConsole ? "console" : "none");
    std::fflush(stdout);
    return true;
  }
  if (act == "profilerdebugger") {
    const std::string r = openDebugger(a);
    std::printf("profiler: debugger %s ok=%d\n", r.c_str(), g_p.dbgOk > 0 ? 1 : 0);
    std::fflush(stdout);
    return true;
  }
  if (act == "profilerwait") {
    std::printf("profiler: wait debugger %s %s\n", g_dw.timedOut ? "TIMEOUT" : "done", g_p.dbgText.c_str());
    std::fflush(stdout);
    g_dw.active = false;
    return true;
  }
  if (act == "profilerport") {
    std::string t;
    int port = 0;
    const bool ok = probeDebugger(t, port);
    g_p.dbgOk = ok ? 1 : -1;
    g_p.dbgText = t;
    g_p.dbgPort = port;
    std::printf("profiler: debugger-probe ok=%d port=%d %s\n", ok ? 1 : 0, port, t.c_str());
    std::fflush(stdout);
    return true;
  }
  return false;
}

}  // namespace editor
}  // namespace rensga
