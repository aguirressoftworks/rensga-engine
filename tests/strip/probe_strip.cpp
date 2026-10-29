#include <string.h>

#include "rensga/core/object.h"
#include "rensga/core/reflect.h"
#include "rensga_test.h"

using namespace rensga;

#ifndef RENSGA_STRIP_EDITOR_METADATA
#error "this test must be built with RENSGA_STRIP_EDITOR_METADATA"
#endif

struct StripProbe : Component {
  REFLECT(StripProbe)
  REQUIRE_COMPONENT(Camera)
  HEADER("Movement")
  TOOLTIP("Units per second")
  RANGE(0, 20)
  SERIALIZE_FIELD(Scalar, speed, 5)
  SERIALIZE_FIELD(i32, lives, 3)
};

TEST(strip_removes_editor_metadata_but_keeps_fields) {
  const TypeInfo* t = StripProbe::staticTypeInfo();
  CHECK(t != nullptr);
  CHECK_EQ(t->fieldCount, 2u);
  CHECK(strcmp(t->fields[0].name, "speed") == 0);
  CHECK(t->fields[0].header == nullptr);
  CHECK(t->fields[0].tooltip == nullptr);
  CHECK(!t->fields[0].hasRange);
  CHECK_EQ(t->requireCount, 0u);
  alignas(16) unsigned char mem[sizeof(StripProbe)];
  t->construct(mem);
  CHECK_EQ(reinterpret_cast<StripProbe*>(mem)->speed, 5.0f);  // defaults and layout stay
  t->destruct(mem);
}
