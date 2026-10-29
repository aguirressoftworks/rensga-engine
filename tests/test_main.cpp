#include "rensga_test.h"

namespace rtest {
Case*& head() {
  static Case* h = nullptr;
  return h;
}
int& failures() {
  static int f = 0;
  return f;
}
}  // namespace rtest

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0, failedCases = 0;
  for (rtest::Case* c = rtest::head(); c; c = c->next) {
    if (filter && !strstr(c->name, filter)) continue;
    int before = rtest::failures();
    c->fn();
    ++ran;
    bool bad = rtest::failures() != before;
    failedCases += bad;
    printf("[%s] %s\n", bad ? "FAIL" : " ok ", c->name);
  }
  printf("%d test(s), %d failed\n", ran, failedCases);
  return failedCases ? 1 : 0;
}
