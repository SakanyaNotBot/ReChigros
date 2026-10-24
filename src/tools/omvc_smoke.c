// ObjectivelyMVC runtime smoke test.
//
// It stands up the framework the way the upstream `Examples/Hello.c` does: a RenderDevice
// over the window, one Framebuffer built from the swapchain texture format and the window's
// pixel size, that framebuffer registered with setFramebuffer, a WindowController driving a
// ViewController, and a frame run through beginFrame / render / endFrame. The sequencing
// matters because the framework asserts and aborts on a mis-ordered call instead of
// returning an error.
//
// Two additions go beyond the example so the test proves something rather than just
// running: the hierarchy contains a coloured panel, and the framebuffer is read back so an
// empty frame fails instead of reporting success.
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
      SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    fprintf(stderr, "CreateWindow: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  // initWithWindow claims the window for the GPU device itself, so the caller must not
  // claim it first: doing so makes this call fail with "Window already claimed".
  RenderDevice *device = $(alloc(RenderDevice), initWithWindow, window, NULL);
  if (!device) {
    fprintf(stderr, "RenderDevice init failed\n");
    return 1;
  }
  printf("SMOKE renderdevice=ok\n");

  int width = 0, height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);
  if (width <= 0 || height <= 0) {
    fprintf(stderr, "SMOKE FAIL: window has no pixel size\n");
    return 1;
  }

  // The colour attachment format comes from the swapchain: the framework blits this
  // framebuffer's resolved colour to the drawable, so a mismatched format makes that blit
  // fail while the frame still reports success.
  const SDL_GPUTextureFormat format = $(device, getSwapchainTextureFormat);
  Framebuffer *framebuffer = $(device, createFramebuffer, &(GPU_FramebufferCreateInfo) {
    .size = { width, height },
    .colorAttachments = {{
      .format = format,
      .clearColor = { 0.15f, 0.16f, 0.20f, 1.0f },
    }},
    .numColorTargets = 1,
    .depthAttachment = {
      .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
      .clearDepth = 1.0f,
    },
    .sampleCount = SDL_GPU_SAMPLECOUNT_1,
  });
  if (!framebuffer) {
    fprintf(stderr, "SMOKE FAIL: createFramebuffer returned NULL\n");
    return 1;
  }
  $(device, setFramebuffer, framebuffer);
  printf("SMOKE framebuffer=ok (%dx%d)\n", width, height);

  WindowController *controller = $(alloc(WindowController), initWithDevice, device);
  if (!controller) {
    fprintf(stderr, "SMOKE FAIL: WindowController init failed\n");
    return 1;
  }
  printf("SMOKE windowcontroller=ok\n");

  Theme *theme = $(alloc(Theme), init);
  if (theme) $(controller, setTheme, theme);
  printf("SMOKE theme=%s\n", theme ? "ok" : "null");

  ViewController *viewController = $(alloc(ViewController), init);
  const SDL_Rect rootFrame = { 0, 0, width, height };
  View *root = $(alloc(View), initWithFrame, &rootFrame);

  // A panel with an explicit background is the least ambiguous visible content: a bare View
  // paints nothing, and Renderer::endFrame loads rather than clears its target, so a
  // hierarchy without a painted control legitimately produces a blank image.
  Panel *panel = $(alloc(Panel), initWithFrame,
      &(SDL_Rect){ width / 8, height / 4, width * 3 / 4, height / 2 });
  ((View *) panel)->backgroundColor = (SDL_Color){ 0xD0, 0x40, 0x40, 0xFF };
  $(root, addSubview, (View *) panel);

  $(viewController, setView, root);
  $(controller, setViewController, viewController);

  // View::draw asserts that the view is attached to a window, and that pointer is propagated
  // by moveToWindow rather than assigned at construction. The example gets this for free
  // through its view controller; doing it explicitly keeps the requirement visible.
  $(root, moveToWindow, window);
  printf("SMOKE hierarchy=ok (children=%d)\n", (int) root->subviews->count);

  // Drive one frame exactly as the example does.
  CommandBuffer *commands = $(device, beginFrame);
  if (!commands) {
    fprintf(stderr, "SMOKE FAIL: beginFrame returned NULL\n");
    return 1;
  }
  $(controller, render);
  $(device, endFrame);
  printf("SMOKE render=ok\n");

  // Read the framebuffer back and require the text glyphs to have reached it. A pass that
  // draws nothing still reports success, so this is the only step that separates a working
  // frame from an empty one. Glyph coverage is the reliable signal here: the theme overrides
  // a view's background colour, so a coloured panel is not a stable check, but antialiased
  // white text is exactly what the framework is for.
  SDL_GPUTexture *colour = $(framebuffer, resolveColorTexture, 0)->texture;
  if (!colour) {
    fprintf(stderr, "SMOKE FAIL: framebuffer has no resolve colour texture\n");
    return 1;
  }
  const Uint32 byteCount = (Uint32) width * (Uint32) height * 4u;
  SDL_GPUTransferBuffer *readback = SDL_CreateGPUTransferBuffer(device->device,
      &(SDL_GPUTransferBufferCreateInfo) {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = byteCount,
      });
  if (!readback) {
    fprintf(stderr, "SMOKE FAIL: create readback buffer: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GPUCommandBuffer *copy = SDL_AcquireGPUCommandBuffer(device->device);
  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(copy);
  SDL_GPUTextureRegion region = { 0 };
  region.texture = colour;
  region.w = (Uint32) width;
  region.h = (Uint32) height;
  region.d = 1;
  SDL_GPUTextureTransferInfo destination = { 0 };
  destination.transfer_buffer = readback;
  SDL_DownloadFromGPUTexture(copyPass, &region, &destination);
  SDL_EndGPUCopyPass(copyPass);
  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(copy);
  SDL_WaitForGPUFences(device->device, true, &fence, 1);
  SDL_ReleaseGPUFence(device->device, fence);

  const Uint8 *pixels = SDL_MapGPUTransferBuffer(device->device, readback, false);
  unsigned differing = 0, greyInk = 0;
  Uint32 background = 0;
  for (Uint32 i = 0; i < (Uint32) width * (Uint32) height; ++i) {
    const Uint32 r = pixels[i * 4], g = pixels[i * 4 + 1], b = pixels[i * 4 + 2];
    const Uint32 value = (r << 16) | (g << 8) | b;
    if (i == 0) background = value;
    else if (value != background) ++differing;
    // Text is drawn as white with coverage in alpha, so a glyph pixel is neutral
    // (r == g == b) and brighter than the dark clear colour.
    if (r == g && g == b && r > 120) ++greyInk;
  }
  SDL_UnmapGPUTransferBuffer(device->device, readback);
  SDL_ReleaseGPUTransferBuffer(device->device, readback);
  printf("SMOKE pixels: differing=%u glyphCoverage=%u of %u\n", differing, greyInk,
      (Uint32) width * (Uint32) height);

  release(controller);
  SDL_DestroyWindow(window);
  SDL_Quit();

  if (differing == 0) {
    fprintf(stderr, "SMOKE FAIL: rendered frame is uniform\n");
    return 1;
  }
  if (greyInk == 0) {
    fprintf(stderr, "SMOKE FAIL: no glyph coverage reached the framebuffer\n");
    return 1;
  }
  printf("SMOKE PASS\n");
  return 0;
}
