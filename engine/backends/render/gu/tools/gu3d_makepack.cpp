// F5.01 dev tool (host): writes the pack3d_builder.h pack to a file. gu3d_makepack [--c16] out.rpak
#include <stdio.h>
#include <string.h>

#include "pack3d_builder.h"

int main(int argc, char** argv) {
  rensga::fixtures::Pack3dBuilder b;
  const char* out = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--c16")) b.c16Cube = true;
    else out = argv[i];
  }
  if (!out) return 2;
  rensga::fixtures::PackImage img;
  b.build(img);
  FILE* f = fopen(out, "wb");
  if (!f) return 3;
  fwrite(img.data, 1, img.size, f);
  fclose(f);
  printf("wrote %s (%zu bytes)\n", out, img.size);
  return 0;
}
