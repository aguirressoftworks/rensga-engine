// Test-only TargetModule: "packages" the project's cooked pack into a stored (uncompressed) zip.
#include <cstdint>
#include <fstream>
#include <iterator>

#include "rensga/editor/target_module.h"

namespace rensga {
namespace editor {
namespace {

namespace fs = std::filesystem;

uint32_t crc32(const std::string& d) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char b : d) {
    c ^= b;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}
void le16(std::string& o, uint16_t v) { o += char(v & 255); o += char(v >> 8); }
void le32(std::string& o, uint32_t v) { le16(o, v & 0xFFFF); le16(o, v >> 16); }

// Single-entry stored zip.
std::string makeZip(const std::string& name, const std::string& data) {
  const uint32_t crc = crc32(data), size = static_cast<uint32_t>(data.size());
  std::string z;
  le32(z, 0x04034b50); le16(z, 20); le16(z, 0); le16(z, 0); le16(z, 0); le16(z, 0x21);
  le32(z, crc); le32(z, size); le32(z, size); le16(z, name.size()); le16(z, 0);
  z += name; z += data;
  const uint32_t cdOff = static_cast<uint32_t>(z.size());
  std::string cd;
  le32(cd, 0x02014b50); le16(cd, 20); le16(cd, 20); le16(cd, 0); le16(cd, 0); le16(cd, 0); le16(cd, 0x21);
  le32(cd, crc); le32(cd, size); le32(cd, size); le16(cd, name.size());
  le16(cd, 0); le16(cd, 0); le16(cd, 0); le16(cd, 0); le32(cd, 0); le32(cd, 0);
  cd += name;
  z += cd;
  le32(z, 0x06054b50); le16(z, 0); le16(z, 0); le16(z, 1); le16(z, 1);
  le32(z, static_cast<uint32_t>(cd.size())); le32(z, cdOff); le16(z, 0);
  return z;
}

struct NullLauncher : pipeline::EmulatorLauncher {
  bool start(const pipeline::LaunchSpec&, EventSink) override { return false; }
  void stop() override {}
  bool running() const override { return false; }
  std::vector<std::string> drainLog() override { return {}; }
};

struct NullPlugin : ICookerFormatPlugin {
  std::string_view id() const override { return "dummy"; }
  bool cookMesh(const PrimitiveMesh&, const BakedLight&, Color32, std::vector<uint8_t>&, uint32_t&,
                std::string& error) override {
    error = "dummy target cooks no meshes";
    return false;
  }
};

class DummyTargetModule : public TargetModule {
 public:
  std::string_view id() const override { return "dummy"; }
  std::string_view displayName() const override { return "Dummy"; }
  ICookerFormatPlugin& cookerPlugin() override { return plugin_; }
  bool validate(const BuildContext&, EventSink) override { return true; }
  bool compile(const BuildContext&, EventSink) override { return true; }
  bool package(const BuildContext& ctx, EventSink) override {
    std::ifstream in(ctx.cookedPack, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::error_code ec;
    fs::create_directories(ctx.buildDir, ec);
    std::ofstream out(ctx.buildDir / "dummy.zip", std::ios::binary);
    const std::string z = makeZip("game.rpak", data);
    out.write(z.data(), static_cast<std::streamsize>(z.size()));
    return static_cast<bool>(out);
  }
  pipeline::EmulatorLauncher& launcher() override { return launcher_; }

 private:
  NullPlugin plugin_;
  NullLauncher launcher_;
};

}  // namespace

RENSGA_REGISTER_TARGET(DummyTargetModule);

}  // namespace editor
}  // namespace rensga
