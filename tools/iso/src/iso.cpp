#include "rensga/iso/iso.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <tuple>
#include <map>
#include <memory>

namespace rensga::iso {
namespace {

struct Node {
  std::string name;  // stored name ("NAME.EXT" for files without ;1, dir name for dirs)
  bool isDir = false;
  std::string source;
  uint64_t size = 0;
  uint32_t extent = 0;
  uint32_t dirSize = 0;  // bytes, sector multiple (dirs)
  int pathNum = 0;       // dirs: 1-based path table index
  Node* parent = nullptr;
  std::vector<std::unique_ptr<Node>> kids;
};

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

bool validPart(const std::string& n, bool dir, std::string& why) {
  if (n.empty()) { why = "empty name"; return false; }
  size_t dot = n.find('.');
  if (dir && dot != std::string::npos) { why = "directory names have no extension"; return false; }
  std::string base = dot == std::string::npos ? n : n.substr(0, dot);
  std::string ext = dot == std::string::npos ? "" : n.substr(dot + 1);
  if (base.size() > 8 || ext.size() > 3 || ext.find('.') != std::string::npos) { why = "not an 8.3 name"; return false; }
  for (char c : n) {
    if (c == '.') continue;
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { why = "character not allowed in ISO 9660 level 1"; return false; }
  }
  return true;
}

void le32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
void be32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (24 - 8 * i)); }
void le16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void be16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void both32(uint8_t* p, uint32_t v) { le32(p, v); be32(p + 4, v); }
void both16(uint8_t* p, uint32_t v) { le16(p, v); be16(p + 2, v); }

struct Date { int y, mo, d, h, mi, s; };
Date civil(int64_t t) {
  if (t < 0) t = 0;
  int64_t days = t / 86400, rem = t % 86400;
  int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
  return {(int)(y + (m <= 2)), m, d, (int)(rem / 3600), (int)(rem % 3600 / 60), (int)(rem % 60)};
}

uint32_t sectorsFor(uint64_t bytes) { return (uint32_t)((bytes + kSector - 1) / kSector); }

// Size of one directory record.
uint32_t recLen(size_t idLen) { uint32_t n = 33 + (uint32_t)idLen; return n + (n & 1); }

std::string storedName(const Node& n) { return n.isDir ? n.name : n.name + ";1"; }

// Directory content: ".", "..", then children sorted by stored name.
std::vector<Node*> sortedKids(const Node& d) {
  std::vector<Node*> v;
  for (auto& k : d.kids) v.push_back(k.get());
  std::sort(v.begin(), v.end(), [](Node* a, Node* b) { return storedName(*a) < storedName(*b); });
  return v;
}

uint32_t dirBytes(const Node& d) {
  uint32_t used = 0, sectors = 1;
  auto add = [&](uint32_t len) {
    if (used + len > kSector) { ++sectors; used = 0; }
    used += len;
  };
  add(recLen(1)); add(recLen(1));
  for (Node* k : sortedKids(d)) add(recLen(storedName(*k).size()));
  return sectors * kSector;
}

struct Writer {
  int64_t ts;
  Date date;

  void rec(std::vector<uint8_t>& buf, uint32_t& used, uint32_t extent, uint32_t size, bool dir, const std::string& id) {
    uint32_t len = recLen(id.size());
    if (used + len > kSector) { buf.resize(buf.size() + (kSector - used), 0); used = 0; }
    size_t off = buf.size();
    buf.resize(off + len, 0);
    uint8_t* p = &buf[off];
    p[0] = (uint8_t)len;
    both32(p + 2, extent);
    both32(p + 10, size);
    p[18] = (uint8_t)(date.y - 1900); p[19] = (uint8_t)date.mo; p[20] = (uint8_t)date.d;
    p[21] = (uint8_t)date.h; p[22] = (uint8_t)date.mi; p[23] = (uint8_t)date.s; p[24] = 0;
    p[25] = dir ? 2 : 0;
    both16(p + 28, 1);
    p[32] = (uint8_t)id.size();
    memcpy(p + 33, id.data(), id.size());
    used += len;
  }

  std::vector<uint8_t> dirData(const Node& d, const Node* parent) {
    std::vector<uint8_t> buf;
    uint32_t used = 0;
    rec(buf, used, d.extent, d.dirSize, true, std::string(1, '\0'));
    rec(buf, used, parent->extent, parent->dirSize, true, std::string(1, '\1'));
    for (Node* k : sortedKids(d)) rec(buf, used, k->extent, k->isDir ? k->dirSize : (uint32_t)k->size, k->isDir, storedName(*k));
    buf.resize(d.dirSize, 0);
    return buf;
  }

  std::vector<uint8_t> pathTable(const std::vector<Node*>& dirs, bool big) {
    std::vector<uint8_t> t;
    for (Node* d : dirs) {
      std::string id = d->parent ? d->name : std::string(1, '\0');
      size_t off = t.size();
      t.resize(off + 8 + id.size() + (id.size() & 1), 0);
      uint8_t* p = &t[off];
      p[0] = (uint8_t)id.size();
      if (big) { be32(p + 2, d->extent); be16(p + 6, d->parent ? d->parent->pathNum : 1); }
      else { le32(p + 2, d->extent); le16(p + 6, d->parent ? d->parent->pathNum : 1); }
      memcpy(p + 8, id.data(), id.size());
    }
    return t;
  }
};

void pad(uint8_t* p, size_t n, const std::string& s) {
  memset(p, ' ', n);
  memcpy(p, s.data(), std::min(n, s.size()));
}

void dateStr(uint8_t* p, const Date& d) {
  char b[20];
  snprintf(b, sizeof b, "%04d%02d%02d%02d%02d%02d00", d.y, d.mo, d.d, d.h, d.mi, d.s);
  memcpy(p, b, 16);
  p[16] = 0;
}

bool writeAll(FILE* f, const void* p, size_t n) { return n == 0 || fwrite(p, 1, n, f) == n; }

}  // namespace

bool writeImage(const std::string& outPath, const std::vector<InputFile>& files, const Options& opt, std::string& err) {
  Node root;
  root.isDir = true;
  // Build the tree.
  std::vector<std::pair<std::string, Node*>> flat;  // iso path (upper, no ;1) -> file node
  for (const InputFile& in : files) {
    std::string p = upper(in.isoPath);
    for (char& c : p) if (c == '\\') c = '/';
    std::vector<std::string> parts;
    size_t s = 0;
    while (s <= p.size()) {
      size_t e = p.find('/', s);
      if (e == std::string::npos) e = p.size();
      if (e > s) parts.push_back(p.substr(s, e - s));
      s = e + 1;
    }
    if (parts.empty()) { err = "empty path for " + in.source; return false; }
    Node* cur = &root;
    for (size_t i = 0; i < parts.size(); ++i) {
      bool last = i + 1 == parts.size();
      std::string why;
      if (!validPart(parts[i], !last, why)) { err = "'" + in.isoPath + "': " + why; return false; }
      Node* next = nullptr;
      for (auto& k : cur->kids) if (k->name == parts[i]) next = k.get();
      if (next && (last || !next->isDir)) { err = "'" + in.isoPath + "': duplicate or conflicting name"; return false; }
      if (!next) {
        cur->kids.push_back(std::make_unique<Node>());
        next = cur->kids.back().get();
        next->name = parts[i]; next->isDir = !last; next->parent = cur;
      }
      cur = next;
    }
    FILE* f = fopen(in.source.c_str(), "rb");
    if (!f) { err = "cannot open " + in.source; return false; }
    fseek(f, 0, SEEK_END);
    long long sz = ftell(f);
    fclose(f);
    if (sz < 0 || sz > 0xFFFFFFFFll) { err = in.source + ": size not supported (max 4 GiB - 1)"; return false; }
    cur->source = in.source; cur->size = (uint64_t)sz;
    std::string full;
    for (size_t i = 0; i < parts.size(); ++i) full += (i ? "/" : "") + parts[i];
    flat.push_back({full, cur});
  }

  // Directories in path table order (BFS, children sorted).
  std::vector<Node*> dirs{&root};
  for (size_t i = 0; i < dirs.size(); ++i) {
    dirs[i]->pathNum = (int)i + 1;
    for (Node* k : sortedKids(*dirs[i])) if (k->isDir) dirs.push_back(k);
  }
  if (dirs.size() > 65535) { err = "too many directories"; return false; }
  Writer w;
  w.ts = opt.timestamp;
  if (w.ts < 0) { const char* e = getenv("SOURCE_DATE_EPOCH"); w.ts = e ? atoll(e) : 0; }
  w.date = civil(w.ts);

  // Layout: 16 system sectors, PVD, terminator, L table, M table, directories, files.
  uint32_t ptBytes = 0;
  for (Node* d : dirs) { size_t n = d->parent ? d->name.size() : 1; ptBytes += (uint32_t)(8 + n + (n & 1)); }
  uint32_t ptSectors = sectorsFor(ptBytes);
  uint32_t lPt = 18, mPt = 18 + ptSectors, next = 18 + 2 * ptSectors;
  for (Node* d : dirs) { d->dirSize = dirBytes(*d); d->extent = next; next += d->dirSize / kSector; }
  // File order: --order entries first, then the rest by path.
  std::vector<Node*> ordered;
  auto taken = [&](Node* n) { return std::find(ordered.begin(), ordered.end(), n) != ordered.end(); };
  for (const std::string& o : opt.order) {
    std::string u = upper(o);
    for (char& c : u) if (c == '\\') c = '/';
    Node* hit = nullptr;
    for (auto& f : flat) if (f.first == u) hit = f.second;
    if (!hit) for (auto& f : flat) if (!hit && f.first.size() > u.size() && f.first.compare(f.first.size() - u.size() - 1, u.size() + 1, "/" + u) == 0) hit = f.second;
    if (!hit) { err = "--order names a file that is not in the image: " + o; return false; }
    if (!taken(hit)) ordered.push_back(hit);
  }
  std::vector<std::pair<std::string, Node*>> rest = flat;
  std::sort(rest.begin(), rest.end());
  for (auto& r : rest) if (!taken(r.second)) ordered.push_back(r.second);
  for (Node* f : ordered) {
    f->extent = next;
    next += sectorsFor(f->size);
  }
  uint32_t total = next;

  FILE* out = fopen(outPath.c_str(), "wb");
  if (!out) { err = "cannot create " + outPath; return false; }
  bool ok = true;
  std::vector<uint8_t> zero(kSector, 0);
  for (int i = 0; i < 16; ++i) ok = ok && writeAll(out, zero.data(), kSector);

  std::vector<uint8_t> lt = w.pathTable(dirs, false), mt = w.pathTable(dirs, true);
  std::vector<uint8_t> pvd(kSector, 0);
  pvd[0] = 1; memcpy(&pvd[1], "CD001", 5); pvd[6] = 1;
  pad(&pvd[8], 32, "");
  pad(&pvd[40], 32, upper(opt.volumeId));
  both32(&pvd[80], total);
  both16(&pvd[120], 1); both16(&pvd[124], 1); both16(&pvd[128], kSector);
  both32(&pvd[132], (uint32_t)lt.size());
  le32(&pvd[140], lPt); be32(&pvd[148], mPt);
  {
    std::vector<uint8_t> rb;
    uint32_t used = 0;
    w.rec(rb, used, root.extent, root.dirSize, true, std::string(1, '\0'));
    memcpy(&pvd[156], rb.data(), 34);
  }
  pad(&pvd[190], 128, ""); pad(&pvd[318], 128, ""); pad(&pvd[446], 128, "RENSGA ENGINE ISO WRITER"); pad(&pvd[574], 128, "");
  pad(&pvd[702], 37, ""); pad(&pvd[739], 37, ""); pad(&pvd[776], 37, "");
  dateStr(&pvd[813], w.date); dateStr(&pvd[830], w.date);
  memset(&pvd[847], '0', 16); pvd[863] = 0;
  dateStr(&pvd[864], w.date);
  pvd[881] = 1;
  ok = ok && writeAll(out, pvd.data(), kSector);
  std::vector<uint8_t> term(kSector, 0);
  term[0] = 255; memcpy(&term[1], "CD001", 5); term[6] = 1;
  ok = ok && writeAll(out, term.data(), kSector);
  lt.resize(ptSectors * kSector, 0); mt.resize(ptSectors * kSector, 0);
  ok = ok && writeAll(out, lt.data(), lt.size()) && writeAll(out, mt.data(), mt.size());
  for (Node* d : dirs) {
    std::vector<uint8_t> dd = w.dirData(*d, d->parent ? d->parent : d);
    ok = ok && writeAll(out, dd.data(), dd.size());
  }
  std::vector<uint8_t> buf(1 << 16);
  for (Node* f : ordered) {
    if (!ok) break;
    FILE* in = fopen(f->source.c_str(), "rb");
    if (!in) { err = "cannot open " + f->source; ok = false; break; }
    uint64_t left = f->size;
    while (left) {
      size_t want = (size_t)std::min<uint64_t>(left, buf.size());
      if (fread(buf.data(), 1, want, in) != want) { err = "short read " + f->source; ok = false; break; }
      ok = ok && writeAll(out, buf.data(), want);
      left -= want;
    }
    fclose(in);
    uint64_t padn = (uint64_t)sectorsFor(f->size) * kSector - f->size;
    ok = ok && writeAll(out, zero.data(), (size_t)padn);
  }
  if (fclose(out) != 0) ok = false;
  if (!ok && err.empty()) err = "write failed: " + outPath;
  return ok;
}

// ------------------------------------------------------------------ reader
namespace {
uint32_t rl32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint32_t rb32(const uint8_t* p) { return p[3] | (p[2] << 8) | (p[1] << 16) | ((uint32_t)p[0] << 24); }
}  // namespace

bool readImage(const std::string& path, Image& out, std::string& err) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) { err = "cannot open " + path; return false; }
  fseek(f, 0, SEEK_END);
  long long fsz = ftell(f);
  auto readSectors = [&](uint32_t lba, uint32_t n, std::vector<uint8_t>& dst) {
    dst.assign((size_t)n * kSector, 0);
    if (fseek(f, (long)((long long)lba * kSector), SEEK_SET) != 0) return false;
    return fread(dst.data(), 1, dst.size(), f) == dst.size();
  };
  struct Closer { FILE* f; ~Closer() { fclose(f); } } closer{f};
  if (fsz % kSector) { err = "image size is not a multiple of 2048"; return false; }
  std::vector<uint8_t> pvd, term;
  if (!readSectors(16, 1, pvd) || !readSectors(17, 1, term)) { err = "image too short"; return false; }
  if (pvd[0] != 1 || memcmp(&pvd[1], "CD001", 5) != 0 || pvd[6] != 1) { err = "bad primary volume descriptor"; return false; }
  if (term[0] != 255 || memcmp(&term[1], "CD001", 5) != 0) { err = "bad volume descriptor terminator"; return false; }
  if (rl32(&pvd[80]) != rb32(&pvd[84])) { err = "volume size not both-endian"; return false; }
  out.totalSectors = rl32(&pvd[80]);
  if ((long long)out.totalSectors * kSector != fsz) { err = "volume space size does not match file size"; return false; }
  if (pvd[128] != 0 || pvd[129] != 8) { err = "logical block size is not 2048"; return false; }
  std::string vid((const char*)&pvd[40], 32);
  while (!vid.empty() && vid.back() == ' ') vid.pop_back();
  out.volumeId = vid;
  uint32_t ptSize = rl32(&pvd[132]), lLba = rl32(&pvd[140]), mLba = rb32(&pvd[148]);
  std::vector<uint8_t> lt, mt;
  uint32_t ptSec = sectorsFor(ptSize);
  if (!readSectors(lLba, ptSec, lt) || !readSectors(mLba, ptSec, mt)) { err = "path table outside image"; return false; }

  struct PT { uint32_t extent; uint32_t parent; std::string name; };
  std::vector<PT> pts;
  for (int pass = 0; pass < 2; ++pass) {
    const std::vector<uint8_t>& t = pass ? mt : lt;
    std::vector<PT> cur;
    for (uint32_t off = 0; off < ptSize;) {
      uint8_t n = t[off];
      if (n == 0 || off + 8 + n > ptSize) { err = "bad path table record"; return false; }
      PT e;
      e.extent = pass ? rb32(&t[off + 2]) : rl32(&t[off + 2]);
      e.parent = pass ? (t[off + 6] << 8 | t[off + 7]) : (t[off + 6] | t[off + 7] << 8);
      e.name.assign((const char*)&t[off + 8], n);
      cur.push_back(e);
      off += 8 + n + (n & 1);
    }
    if (pass == 0) pts = cur;
    else {
      if (cur.size() != pts.size()) { err = "L and M path tables differ in length"; return false; }
      for (size_t i = 0; i < cur.size(); ++i)
        if (cur[i].extent != pts[i].extent || cur[i].parent != pts[i].parent || cur[i].name != pts[i].name) { err = "L and M path tables differ"; return false; }
    }
  }

  // Walk directories from the root record; every directory must appear in the path table with the same extent.
  size_t visitedDirs = 0;
  std::vector<std::tuple<uint32_t, uint32_t, std::string>> stack;
  const uint8_t* rr = &pvd[156];
  stack.push_back({rl32(rr + 2), rl32(rr + 10), ""});
  if (pts.empty() || pts[0].extent != rl32(rr + 2)) { err = "root extent differs from path table"; return false; }
  while (!stack.empty()) {
    auto [ext, size, prefix] = stack.back();
    stack.pop_back();
    ++visitedDirs;
    if (size == 0 || size % kSector || ext + size / kSector > out.totalSectors) { err = "directory extent outside image: " + prefix; return false; }
    std::vector<uint8_t> d;
    if (!readSectors(ext, size / kSector, d)) { err = "cannot read directory " + prefix; return false; }
    std::vector<std::tuple<uint32_t, uint32_t, std::string>> subs;
    uint32_t off = 0;
    std::string prevName;
    while (off < size) {
      uint8_t len = d[off];
      if (len == 0) { off = (off / kSector + 1) * kSector; continue; }
      if (off % kSector + len > kSector || len < 34) { err = "directory record crosses a sector: " + prefix; return false; }
      const uint8_t* p = &d[off];
      if (rl32(p + 2) != rb32(p + 6) || rl32(p + 10) != rb32(p + 14)) { err = "record not both-endian in " + prefix; return false; }
      uint32_t e = rl32(p + 2), sz = rl32(p + 10);
      bool dir = p[25] & 2;
      std::string id((const char*)p + 33, p[32]);
      off += len;
      if (id.size() == 1 && (id[0] == 0 || id[0] == 1)) continue;
      if (id < prevName) { err = "directory records not sorted in " + prefix; return false; }
      prevName = id;
      Entry en;
      if (!dir) {
        size_t semi = id.rfind(';');
        if (semi != std::string::npos) id.resize(semi);
      }
      en.path = prefix.empty() ? id : prefix + "/" + id;
      en.isDir = dir; en.extent = e; en.size = sz;
      if (!dir && sz && (uint64_t)e + sectorsFor(sz) > out.totalSectors) { err = "file extent outside image: " + en.path; return false; }
      out.entries.push_back(en);
      if (dir) subs.push_back({e, sz, en.path});
    }
    for (auto it = subs.rbegin(); it != subs.rend(); ++it) stack.push_back(*it);
  }
  if (visitedDirs != pts.size()) { err = "directory count differs from path table"; return false; }
  std::sort(out.entries.begin(), out.entries.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
  return true;
}

}  // namespace rensga::iso
