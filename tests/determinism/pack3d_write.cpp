// F5 determinism helper: writes the pack3d_builder fixture (lit cube, skinned bar with a looping 2-frame clip, a Light, CTG2) to a file
// so tools/ci/determinism.sh 3dlit can run the same shared-3D pack on Linux and in the PSP simulation ELF.
//   pack3d_write <out.rpak>
#include <stdio.h>

#include "fixtures/pack3d_builder.h"

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: pack3d_write <out.rpak>\n"); return 1; }
  rensga::registerBuiltinComponents();
  rensga::fixtures::Pack3dBuilder b;
  rensga::fixtures::PackImage img;
  b.build(img);
  FILE* f = fopen(argv[1], "wb");
  if (!f) { perror(argv[1]); return 2; }
  const bool ok = fwrite(img.data, 1, img.size, f) == img.size;
  fclose(f);
  return ok ? 0 : 3;
}
