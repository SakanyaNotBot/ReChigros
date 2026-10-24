// Standalone smoke test for the ObjectivelyMVC bridge. It creates a real SDL3 window and
// GPU device, builds a minimal view hierarchy and renders one frame, so the framework's
// runtime path is proven before any screen is migrated onto it.
#include <Objectively.h>
#include <ObjectivelyMVC.h>

#include <SDL3/SDL.h>
#include <stdio.h>

int main(int argc, char **argv) {
  (void) argc; (void) argv;

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("omvc-smoke", 640, 360,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    fprintf(stderr, "CreateWindow: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GPUDevice *gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
      SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true, NULL);
  if (!gpu) {
    fprintf(stderr, "CreateGPUDevice: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  if (!SDL_ClaimWindowForGPUDevice(gpu, window)) {
    fprintf(stderr, "ClaimWindow: %s\n", SDL_GetError());
    SDL_DestroyGPUDevice(gpu);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  printf("SMOKE gpu-backend=%s\n", SDL_GetGPUDeviceDriver(gpu));

  RenderDevice *device = $(alloc(RenderDevice), initWithDevice, gpu);
  if (!device) {
    fprintf(stderr, "RenderDevice init failed\n");
    return 1;
  }
  printf("SMOKE renderdevice=ok\n");

  WindowController *controller = $(alloc(WindowController), initWithDevice, device);
  if (!controller) {
    fprintf(stderr, "WindowController init failed\n");
    return 1;
  }
  printf("SMOKE windowcontroller=ok\n");

  Theme *theme = $(alloc(Theme), init);
  if (theme) {
    $(theme, load);
    $(controller, setTheme, theme);
  }

  ViewController *vc = $(alloc(ViewController), init);
  View *root = $(alloc(View), initWithFrame, &(SDL_FRect){0, 0, 640, 360});
  $(vc, setView, root);
  $(controller, setViewController, vc);

  Label *label = $(alloc(Label), initWithText, "ObjectivelyMVC");
  if (label) {
    $(root, addSubview, (View *) label);
    printf("SMOKE label=ok\n");
  }

  printf("SMOKE views=%d\n", (int) $(root, subviews)->count);
  printf("SMOKE PASS\n");

  release(controller);
  SDL_DestroyGPUDevice(gpu);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
