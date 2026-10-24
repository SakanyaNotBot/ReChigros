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
  View *root;
  StackView *list;
  Font *bodyFont;
  Font *headingFont;
  int tab;
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

static Label *makeLabel(const char *text, Font *font, const char *className) {
  Label *label = $(alloc(Label), initWithText, text, font);
  if (label == NULL) return NULL;
  if (className != NULL) {
    $((View *) label, addClassName, className);
  }
  return label;
}

// C ABI entry points. The C++ side declares these inside an `extern "C"` block, so the names
// must not be mangled.
bool rechigros_mvc_settings_available(void) { return true; }

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
}
