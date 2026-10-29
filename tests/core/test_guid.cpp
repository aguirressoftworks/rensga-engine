#include "rensga/core/guid.h"
#include "rensga_test.h"

using namespace rensga;

TEST(guid_parse_format_roundtrip) {
  const char* text = "00000000-0000-4000-8000-00000000c001";
  Guid g;
  CHECK(Guid::parse(text, g));
  CHECK(!g.isNull());
  CHECK_EQ(g.bytes[6], 0x40);
  CHECK_EQ(g.bytes[15], 0x01);
  char out[Guid::kTextLength + 1];
  g.format(out);
  CHECK(strcmp(out, text) == 0);
}

TEST(guid_uppercase_and_errors) {
  Guid g;
  CHECK(Guid::parse("ABCDEF01-2345-6789-ABCD-EF0123456789", g));
  char out[37];
  g.format(out);
  CHECK(strcmp(out, "abcdef01-2345-6789-abcd-ef0123456789") == 0);
  Guid bad;
  CHECK(!Guid::parse("", bad));
  CHECK(!Guid::parse("00000000-0000-4000-8000-00000000c00", bad));    // short
  CHECK(!Guid::parse("00000000-0000-4000-8000-00000000c0011", bad));  // long
  CHECK(!Guid::parse("00000000x0000-4000-8000-00000000c001", bad));   // bad dash
  CHECK(!Guid::parse("0000000g-0000-4000-8000-00000000c001", bad));   // bad hex
  CHECK(bad.isNull());
  CHECK(Guid().isNull());
}
