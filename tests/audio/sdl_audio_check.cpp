// Plays a looping square wave through SdlAudio with SDL_AUDIODRIVER=disk and checks that the
// device output file is not silent. Needs no display.
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "rensga/audio/sdl_audio.h"

using namespace rensga;

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "sdl_out.raw";
  setenv("SDL_AUDIODRIVER", "disk", 1);
  setenv("SDL_DISKAUDIOFILE", out, 1);
  remove(out);

  static i16 wave[220];
  for (int i = 0; i < 220; ++i) wave[i] = (i / 55) & 1 ? -12000 : 12000;
  AudioClip clip;
  clip.samples = wave;
  clip.frames = 220;
  clip.sampleRate = 22050;
  clip.channels = 1;

  static AudioMixer mixer;
  SdlAudio audio;
  if (!mixer.init(22050, nullptr) || !audio.init(22050, &mixer)) {
    printf("rensga: audio-sdl-check FAIL init\n");
    return 1;
  }
  mixer.play(&clip, 1.0f, 0.0f, true);
  for (int i = 0; i < 150; ++i) {  // about 1.2 s
    audio.update();
    SDL_Delay(8);
  }
  audio.shutdown();

  FILE* f = fopen(out, "rb");
  if (!f) { printf("rensga: audio-sdl-check FAIL no output file\n"); return 1; }
  long total = 0, loud = 0;
  i16 s;
  while (fread(&s, sizeof(s), 1, f) == 1) {
    ++total;
    if (s > 4000 || s < -4000) ++loud;
  }
  fclose(f);
  if (total < 22050 || loud < total / 4) {
    printf("rensga: audio-sdl-check FAIL samples=%ld loud=%ld\n", total, loud);
    return 1;
  }
  printf("rensga: audio-sdl-check ok samples=%ld loud=%ld\n", total, loud);
  return 0;
}
