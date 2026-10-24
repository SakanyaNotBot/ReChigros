// Probe for the ObjectivelyMVC settings page.
//
// It stands the page up off-screen, draws one frame and reads the framebuffer back. The
// readback is the point: a correctly sequenced render that paints nothing still reports
// success, so the page counts as working only when pixels actually changed.
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

// C-ABI entry points exported by the settings page implementation.
bool rechigros_mvc_settings_begin(void *sdlWindow, int width, int height, int tab);
bool rechigros_mvc_settings_draw(void);
int rechigros_mvc_settings_row_count(void);
int rechigros_mvc_settings_read_pixels(unsigned char *destination, int capacity);
void rechigros_mvc_settings_show_tab(int tab);
void rechigros_mvc_settings_shutdown(void);

int main(int argc, char **argv) {
  (void) argc; (void) argv;

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  const int width = 960, height = 540;
  SDL_Window *window = SDL_CreateWindow("settings-probe", width, height,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    fprintf(stderr, "CreateWindow: %s\n", SDL_GetError());
    return 1;
  }

  if (!rechigros_mvc_settings_begin(window, width, height, 0)) {
    fprintf(stderr, "PROBE FAIL: the settings page did not come up\n");
    return 1;
  }
  printf("PROBE rows=%d\n", rechigros_mvc_settings_row_count());

  if (!rechigros_mvc_settings_draw()) {
    fprintf(stderr, "PROBE FAIL: draw returned false\n");
    return 1;
  }

  const int byteCount = width * height * 4;
  unsigned char *pixels = malloc((size_t) byteCount);
  if (rechigros_mvc_settings_read_pixels(pixels, byteCount) != byteCount) {
    fprintf(stderr, "PROBE FAIL: readback failed\n");
    return 1;
  }

  // Count distinct colours and how many pixels differ from the clear colour. Rows and text
  // both show up as distinct colours, so a non-trivial count means the list was drawn.
  unsigned differing = 0, bright = 0;
  const unsigned char bgR = pixels[0], bgG = pixels[1], bgB = pixels[2];
  for (int i = 1; i < width * height; ++i) {
    const unsigned char r = pixels[i * 4], g = pixels[i * 4 + 1], b = pixels[i * 4 + 2];
    if (r != bgR || g != bgG || b != bgB) ++differing;
    if (r > 140 && g > 140 && b > 140) ++bright;
  }
  printf("PROBE pixels: differing=%u bright=%u of %d\n", differing, bright, width * height);

  rechigros_mvc_settings_shutdown();
  free(pixels);
  SDL_DestroyWindow(window);
  SDL_Quit();

  if (differing == 0) {
    fprintf(stderr, "PROBE FAIL: the page drew nothing\n");
    return 1;
  }
  if (bright == 0) {
    fprintf(stderr, "PROBE FAIL: no label text reached the framebuffer\n");
    return 1;
  }
  printf("PROBE PASS\n");
  return 0;
}
