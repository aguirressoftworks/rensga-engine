// Foundation stub of the `rensga` CLI. The pipeline role implements validate/cook/build/play.
#include <cstdio>

int main(int argc, char**) {
  std::fprintf(stderr, "rensga: usage: rensga <validate|cook|build|play> --target psp [--json] [project]\n");
  if (argc > 1) std::fprintf(stderr, "rensga: not implemented\n");
  return argc > 1 ? 1 : 0;
}
