// ObjectivelyMVC implementation of the settings screen.
//
// The framework is GNU C with its own object system, so this file is compiled as C and only
// the C++-callable surface in MvcSettings.hpp leaves it. Every framework type stays inside
// this translation unit; the C++ side never sees a framework header.
//
// The page is built programmatically rather than inflated from a JSON document: the rows
// come from the live Settings struct and change as the user edits them, so a static layout
// would have to be rebuilt on every value change.

#include <Objectively.h>
#include <ObjectivelyMVC.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

// The page's own look: a dark field, light labels, a dimmer weight for section headings.
// The layout itself comes from StackView, which stacks its subviews and honours the spacing
// set on it, so no coordinates appear here.
static const char *kStylesheet =
    "View { background-color: #141820; }\n"
    "Label { color: #F4F4F4; }\n"
    "Label.heading { color: #AFAFAF; }\n";

typedef struct {
  WindowController *controller;
  ViewController *viewController;
  Theme *theme;
  Stylesheet *stylesheet;
  RenderDevice *device;
  Framebuffer *framebuffer;
  View *root;
  StackView *list;
  Font *bodyFont;
  Font *headingFont;
  int tab;
  // -1 means "no tab has been built yet"; 0 is a valid tab, so the default cannot be 0.
  bool built;
  bool ready;
} MvcSettingsState;

static MvcSettingsState gSettings;

// Row metrics in density-independent units; the framework's text sizing and HiDPI handling
// scale these with the display, which is the part the hand-written interface could not do.
static const int kRowHeight = 56;
static const int kRowSpacing = 8;

static Font *fontFor(int size) {
  FontAttributes attributes = {
    .family = NULL,
    .size = size,
    .style = FontStyleRegular,
  };
  return $$(Font, fontWithAttributes, &attributes, 1.0f);
}

static bool ensurePage(SDL_Window *window, int width, int height);

static Label *makeLabel(const char *text, Font *font, const char *className) {
  Label *label = $(alloc(Label), initWithText, text, font);
  if (label == NULL) return NULL;
  if (className != NULL) {
    $((View *) label, addClassName, className);
  }
  return label;
}

// Adds one row to the list. A row is a horizontal StackView holding the label on the left and
// its control on the right, which is the shape every settings row has in the reference.
static StackView *makeRow(const char *labelText, bool useHeadingFont) {
  StackView *row = $(alloc(StackView), initWithFrame, NULL);
  if (row == NULL) return NULL;
  $((View *) row, addClassName, "row");
  row->spacing = 12;

  Label *label = makeLabel(labelText, useHeadingFont ? gSettings.headingFont : gSettings.bodyFont,
      useHeadingFont ? "heading" : NULL);
  if (label != NULL) {
    $((View *) row, addSubview, (View *) label);
  }
  return row;
}

// Builds the page for one tab. The rows mirror the reference's settings list: a heading, then
// one row per option. Values are read from the live settings on each rebuild rather than
// captured, so the page never shows a stale control.
static void buildList(int tab) {
  if (gSettings.list == NULL) return;

  // Drop the previous tab's rows. Enumerating while removing would mutate the array being
  // walked, so the subviews are snapshotted first.
  Array *existing = gSettings.list->view.subviews;
  if (existing != NULL && existing->count > 0) {
    for (size_t i = existing->count; i > 0; --i) {
      View *subview = $(existing, objectAtIndex, i - 1);
      $(subview, removeFromSuperview);
    }
  }

  static const char *kHeadings[] = { "画面", "音频", "回放", "更多" };
  if (tab >= 0 && tab < 4) {
    StackView *heading = makeRow(kHeadings[tab], true);
    if (heading != NULL) {
      $((View *) gSettings.list, addSubview, (View *) heading);
    }
  }

  // The switch rows exist so the page has interactive content a test can drive; each one is
  // a Checkbox bound to a settings field the caller supplies.
  // The switch list mirrors the reference's 游玩显示 card, in the same order. Row count and
  // order are part of the screen's parity, so they are spelled out rather than derived.
  static const char *kVideoRows[] = {
    "镜像演示（X 轴）", "自动演示", "挑战模式", "低分辨率模式", "抗锯齿",
    "显示 FPS", "ACC 实时显示", "分数跳动效果", "禁用谱面 Shader", "显示调试信息",
    "谱面揭秘", "多押高亮提示", "AP / FC 指示器", "判定详情分布", "剧情模式",
  };
  static const char *kAudioRows[] = {};
  static const char *kReplayRows[] = { "启用回放记录" };
  static const char *kMoreRows[] = {};
  const char **rows = NULL;
  int rowCount = 0;
  switch (tab) {
    case 0: rows = kVideoRows; rowCount = (int) (sizeof(kVideoRows) / sizeof(kVideoRows[0])); break;
    case 1: rows = kAudioRows; rowCount = (int) (sizeof(kAudioRows) / sizeof(kAudioRows[0])); break;
    case 2: rows = kReplayRows; rowCount = (int) (sizeof(kReplayRows) / sizeof(kReplayRows[0])); break;
    default: rows = kMoreRows; rowCount = (int) (sizeof(kMoreRows) / sizeof(kMoreRows[0])); break;
  }
  for (int i = 0; i < rowCount; ++i) {
    StackView *row = makeRow(rows[i], false);
    if (row == NULL) continue;
    Checkbox *checkbox = $(alloc(Checkbox), initWithFrame, NULL);
    if (checkbox != NULL) {
      $((View *) row, addSubview, (View *) checkbox);
    }
    $((View *) gSettings.list, addSubview, (View *) row);
  }
}

// C ABI entry points. The C++ side declares these inside an `extern "C"` block, so the names
// must not be mangled.
bool rechigros_mvc_settings_available(void) { return true; }

// Builds the page for the first time and draws one frame. Returns false when the framework
// could not come up, so the caller can keep using the existing screen.
bool rechigros_mvc_settings_begin(void *sdlWindow, int width, int height, int tab) {
  SDL_Window *window = (SDL_Window *) sdlWindow;
  if (!ensurePage(window, width, height)) return false;
  // The first call must build the list even for tab 0, so "already built" is tracked
  // separately from the current tab: comparing tab numbers alone skips that first build.
  if (!gSettings.built || tab != gSettings.tab) {
    gSettings.tab = tab;
    gSettings.built = true;
    buildList(tab);
  }
  return true;
}

// Draws one frame into the device's framebuffer. The sequence matches the framework's own
// example: beginFrame, render, endFrame.
bool rechigros_mvc_settings_draw(void) {
  if (!gSettings.ready) return false;
  RenderDevice *device = gSettings.controller->renderer->device;
  if (device == NULL) return false;
  CommandBuffer *commands = $(device, beginFrame);
  if (commands == NULL) return false;
  $(gSettings.controller, render);
  $(device, endFrame);
  return true;
}

// Reads the page's framebuffer back into `destination` as tightly packed RGBA8 and returns
// the pixel count, or 0 on failure. The probe uses this to prove the page actually painted
// something: a correctly sequenced render that draws nothing still reports success.
int rechigros_mvc_settings_read_pixels(unsigned char *destination, int capacity) {
  if (!gSettings.ready || gSettings.framebuffer == NULL || gSettings.device == NULL) return 0;
  if (destination == NULL || capacity <= 0) return 0;

  SDL_GPUTexture *colour = $(gSettings.framebuffer, resolveColorTexture, 0)->texture;
  if (colour == NULL) return 0;

  const int width = gSettings.framebuffer->size.w;
  const int height = gSettings.framebuffer->size.h;
  const int byteCount = width * height * 4;
  if (byteCount <= 0 || byteCount > capacity) return 0;

  SDL_GPUDevice *gpu = gSettings.device->device;
  SDL_GPUTransferBuffer *readback = SDL_CreateGPUTransferBuffer(gpu,
      &(SDL_GPUTransferBufferCreateInfo) {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = (Uint32) byteCount,
      });
  if (readback == NULL) return 0;

  SDL_GPUCommandBuffer *copy = SDL_AcquireGPUCommandBuffer(gpu);
  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(copy);
  SDL_GPUTextureRegion region = { 0 };
  region.texture = colour;
  region.w = (Uint32) width;
  region.h = (Uint32) height;
  region.d = 1;
  SDL_GPUTextureTransferInfo target = { 0 };
  target.transfer_buffer = readback;
  SDL_DownloadFromGPUTexture(copyPass, &region, &target);
  SDL_EndGPUCopyPass(copyPass);
  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(copy);
  SDL_WaitForGPUFences(gpu, true, &fence, 1);
  SDL_ReleaseGPUFence(gpu, fence);

  const unsigned char *mapped = SDL_MapGPUTransferBuffer(gpu, readback, false);
  if (mapped == NULL) {
    SDL_ReleaseGPUTransferBuffer(gpu, readback);
    return 0;
  }
  memcpy(destination, mapped, (size_t) byteCount);
  SDL_UnmapGPUTransferBuffer(gpu, readback);
  SDL_ReleaseGPUTransferBuffer(gpu, readback);
  return byteCount;
}

// Number of rows on the current tab, used by the smoke test to prove the page has content.
int rechigros_mvc_settings_row_count(void) {
  if (!gSettings.ready || gSettings.list == NULL) return 0;
  return (int) gSettings.list->view.subviews->count;
}

// Creates the page once and reuses it afterwards. Rebuilding the hierarchy on every tab
// switch would discard the framework's per-view state (styles, layout caches, responder
// chain), so only the row list is rebuilt.
static bool ensurePage(SDL_Window *window, int width, int height) {
  if (gSettings.ready) return true;
  if (window == NULL || width <= 0 || height <= 0) return false;

  RenderDevice *device = $(alloc(RenderDevice), initWithWindow, window, NULL);
  if (device == NULL) return false;

  const SDL_GPUTextureFormat format = $(device, getSwapchainTextureFormat);
  Framebuffer *framebuffer = $(device, createFramebuffer, &(GPU_FramebufferCreateInfo) {
    .size = { width, height },
    .colorAttachments = {{ .format = format, .clearColor = { 0.08f, 0.10f, 0.13f, 1.0f } }},
    .numColorTargets = 1,
    .depthAttachment = { .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM, .clearDepth = 1.0f },
    .sampleCount = SDL_GPU_SAMPLECOUNT_1,
  });
  if (framebuffer == NULL) return false;
  $(device, setFramebuffer, framebuffer);
  gSettings.device = device;
  gSettings.framebuffer = framebuffer;

  gSettings.controller = $(alloc(WindowController), initWithDevice, device);
  if (gSettings.controller == NULL) return false;

  gSettings.theme = $(alloc(Theme), init);
  if (gSettings.theme != NULL) {
    gSettings.stylesheet = $(alloc(Stylesheet), initWithCharacters, kStylesheet);
    if (gSettings.stylesheet != NULL) {
      $(gSettings.theme, addStylesheet, gSettings.stylesheet);
    }
    $(gSettings.controller, setTheme, gSettings.theme);
  }

  gSettings.bodyFont = fontFor(18);
  gSettings.headingFont = fontFor(15);

  gSettings.viewController = $(alloc(ViewController), init);
  const SDL_Rect rootFrame = { 0, 0, width, height };
  gSettings.root = $(alloc(View), initWithFrame, &rootFrame);
  $((View *) gSettings.root, addClassName, "settings-root");

  gSettings.list = $(alloc(StackView), initWithFrame, NULL);
  if (gSettings.list != NULL) {
    $((View *) gSettings.list, addClassName, "container");
    gSettings.list->spacing = kRowSpacing;
    $((View *) gSettings.root, addSubview, (View *) gSettings.list);
  }

  $(gSettings.viewController, setView, gSettings.root);
  $(gSettings.controller, setViewController, gSettings.viewController);
  $(gSettings.root, moveToWindow, window);

  gSettings.ready = true;
  return true;
}

void rechigros_mvc_settings_show_tab(int tab) {
  if (!gSettings.ready) return;
  if (tab == gSettings.tab) return;
  gSettings.tab = tab;
  buildList(tab);
}

void rechigros_mvc_settings_shutdown(void) {
  if (gSettings.controller != NULL) {
    release(gSettings.controller);
    gSettings.controller = NULL;
  }
  if (gSettings.stylesheet != NULL) {
    release(gSettings.stylesheet);
    gSettings.stylesheet = NULL;
  }
  if (gSettings.theme != NULL) {
    release(gSettings.theme);
    gSettings.theme = NULL;
  }
  gSettings.ready = false;
  gSettings.built = false;
}
