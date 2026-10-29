// Tiny self-made test runner (no gtest). Register with TEST(name) { CHECK(...); }.
#pragma once
#include <stdio.h>
#include <string.h>

namespace rtest {
struct Case {
  const char* name;
  void (*fn)();
  Case* next;
};
Case*& head();
int& failures();
struct Reg {
  Reg(Case* c) {
    Case*& h = head();
    c->next = nullptr;
    Case** link = &h;
    while (*link) link = &(*link)->next;
    *link = c;
  }
};
}  // namespace rtest

#define TEST(name)                                                      \
  static void test_##name();                                            \
  static rtest::Case case_##name = {#name, &test_##name, nullptr};      \
  static rtest::Reg reg_##name(&case_##name);                           \
  static void test_##name()

#define CHECK(cond)                                                                      \
  do {                                                                                   \
    if (!(cond)) {                                                                       \
      ++rtest::failures();                                                               \
      fprintf(stderr, "  CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__);        \
    }                                                                                    \
  } while (0)
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NEAR(a, b, eps) CHECK(((a) - (b)) < (eps) && ((b) - (a)) < (eps))
