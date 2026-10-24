// Standalone smoke test for the ObjectivelyMVC bridge.
//
// It creates a real SDL3 window and GPU device, builds a minimal view hierarchy and renders
// one frame, so the framework's runtime path is proven before any screen is migrated onto it.
// The dispatch macro $(obj, method) resolves the method table from the receiver's static type,
// so every receiver below is declared with its concrete class, never a base pointer.
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
  // RenderDevice::initWithWindow claims the window for the GPU device itself, so claiming it
  // here first makes that call fail with "Window already claimed" and the process then tears
  // down a device that is still in use.
  printf("SMOKE gpu-backend=%s\n", SDL_GetGPUDeviceDriver(gpu));

  RenderDevice *device = $(alloc(RenderDevice), initWithWindow, window, NULL);
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
  printf("SMOKE theme=%s\n", theme ? "ok" : "null");
  if (theme) $(controller, setTheme, theme);

  ViewController *vc = $(alloc(ViewController), init);
  const SDL_Rect rootFrame = {0, 0, 640, 360};
  View *root = $(alloc(View), initWithFrame, &rootFrame);
  // A Panel with an explicit background is the least ambiguous visible content: an empty
  // View draws nothing, and Renderer::endFrame loads (does not clear) the target, so a
  // hierarchy without a painted control legitimately produces an all-zero image.
  Panel *panel = $(alloc(Panel), initWithFrame, &(SDL_Rect){ 40, 40, 560, 280 });
  // The theme leaves a bare Panel unshaded, so its background is set explicitly here: this
  // test needs proof that the hierarchy's geometry reaches the target, not proof that the
  // stylesheet happens to give a Panel a fill.
  ((View *) panel)->backgroundColor = (SDL_Color){ 0xD0, 0x40, 0x40, 0xFF };
  $(root, addSubview, (View *) panel);
  $(vc, setView, root);
  $(controller, setViewController, vc);
  // View::draw asserts that the view has a window, and the pointer is propagated by
  // moveToWindow rather than assigned when the view is created. Without this step the
  // hierarchy is built and laid out but every draw is skipped, which reads back as a blank
  // frame that still reports success.
  $(root, moveToWindow, window);
  // layoutIfNeeded runs inside renderTo, so the frames are inspected after it by printing
  // them from here too: a zero-sized clipping frame is the one condition under which
  // Renderer::drawView silently skips a view.
  printf("SMOKE frames: root=%dx%d panel=%dx%d\n",
      root->frame.w, root->frame.h,
      ((View *) panel)->frame.w, ((View *) panel)->frame.h);
  printf("SMOKE view-hierarchy=ok (children=%d)\n", (int) root->subviews->count);

  // Render one real frame into an off-screen target. Until this step the test only proved
  // the objects could be allocated; drawing together with a resolved target exercises the
  // GPU pipeline, the theme's stylesheet and the view hierarchy, which is what a migrated
  // screen will actually depend on. Rendering off-screen rather than to the swapchain is
  // what lets this run under SDL_VIDEODRIVER=offscreen, where there is no drawable.
  Framebuffer *target = $(device, createFramebuffer, &(GPU_FramebufferCreateInfo) {
    .size = { 640, 360 },
    .colorAttachments = {{
      .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      // A non-black clear makes "the pass ran" distinguishable from "the pass drew
      // nothing": without it a successful but empty frame and a never-executed pass both
      // read back as zeros.
      .clearColor = { 0.15f, 0.16f, 0.20f, 1.0f },
    }},
    .numColorTargets = 1,
    .sampleCount = SDL_GPU_SAMPLECOUNT_1,
  });
  if (!target) {
    fprintf(stderr, "SMOKE FAIL: createFramebuffer returned NULL\n");
    return 1;
  }
  // Clear the target first. Renderer::endFrame begins its pass with SDL_GPU_LOADOP_LOAD, so
  // it preserves whatever the target already holds and never clears it; an uninitialised
  // target stays at zero no matter what the hierarchy draws.
  SDL_GPUCommandBuffer *clearCommands = SDL_AcquireGPUCommandBuffer(gpu);
  SDL_GPURenderPass *clearPass = SDL_BeginGPURenderPass(clearCommands,
      &(SDL_GPUColorTargetInfo) {
        .texture = $(target, resolveColorTexture, 0)->texture,
        .clear_color = { 0.15f, 0.16f, 0.20f, 1.0f },
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
      }, 1, NULL);
  SDL_EndGPURenderPass(clearPass);
  SDL_SubmitGPUCommandBuffer(clearCommands);

  CommandBuffer *commands = $(device, acquireCommandBuffer);
  if (!commands) {
    fprintf(stderr, "SMOKE FAIL: acquireCommandBuffer returned NULL\n");
    return 1;
  }
  $(controller, renderTo, commands, target);
  // renderTo only records the frame into the command buffer; nothing is submitted until the
  // device ends the frame. Without this the target stays at its clear value and the readback
  // below sees a uniform image.
  SDL_SubmitGPUCommandBuffer(commands->commands);
  printf("SMOKE render=ok\n");

  // Prove the frame actually contains pixels. A render pass that draws nothing still
  // reports success, so the target is read back and checked for a non-uniform image: the
  // theme paints a background, so uniform output means the hierarchy never drew.
  SDL_GPUTexture *colour = $(target, resolveColorTexture, 0)->texture;
  if (!colour) {
    fprintf(stderr, "SMOKE FAIL: no resolve colour texture\n");
    return 1;
  }
  const Uint32 bytesPerPixel = 4;
  const Uint32 width = 640, height = 360;
  const Uint32 byteCount = width * height * bytesPerPixel;
  SDL_GPUTransferBuffer *readback = SDL_CreateGPUTransferBuffer(gpu,
      &(SDL_GPUTransferBufferCreateInfo) {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = byteCount,
      });
  SDL_GPUCommandBuffer *copyCommands = SDL_AcquireGPUCommandBuffer(gpu);
  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(copyCommands);
  SDL_GPUTextureRegion region = {0};
  region.texture = colour;
  region.w = width;
  region.h = height;
  region.d = 1;
  SDL_GPUTextureTransferInfo destination = {0};
  destination.transfer_buffer = readback;
  SDL_DownloadFromGPUTexture(copyPass, &region, &destination);
  SDL_EndGPUCopyPass(copyPass);
  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(copyCommands);
  SDL_WaitForGPUFences(gpu, true, &fence, 1);
  SDL_ReleaseGPUFence(gpu, fence);
  const Uint8 *pixels = SDL_MapGPUTransferBuffer(gpu, readback, false);
  unsigned distinct = 0;
  unsigned nonBlack = 0;
  Uint32 first = 0;
  for (Uint32 i = 0; i < width * height; ++i) {
    const Uint32 value = (Uint32) pixels[i * 4] << 16 | (Uint32) pixels[i * 4 + 1] << 8
        | (Uint32) pixels[i * 4 + 2];
    if (i == 0) first = value;
    else if (value != first) ++distinct;
    if (value != 0) ++nonBlack;
  }
  SDL_UnmapGPUTransferBuffer(gpu, readback);
  SDL_ReleaseGPUTransferBuffer(gpu, readback);
  printf("SMOKE pixels: differing=%u nonBlack=%u of %u\n", distinct, nonBlack, width * height);
  if (distinct == 0) {
    fprintf(stderr, "SMOKE FAIL: rendered frame is uniform\n");
    return 1;
  }

  printf("SMOKE PASS\n");

  release(controller);
  SDL_DestroyGPUDevice(gpu);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
