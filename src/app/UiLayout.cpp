#include "rechigros/app/UiLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace rechigros::app::ui {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kDegreesToRadians = kPi / 180.0F;

ImVec2 framebufferScale() {
  if (ImGui::GetCurrentContext() == nullptr) return ImVec2(1.0F, 1.0F);
  const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
  return ImVec2(std::isfinite(scale.x) && scale.x > 0.0F ? scale.x : 1.0F,
      std::isfinite(scale.y) && scale.y > 0.0F ? scale.y : 1.0F);
}

struct PanelShape {
  ImVec2 points[4]{};
  float width{};
  float height{};
  float skew{};
};

PanelShape panelShape(const Canvas& c, const Box& designBox, const PanelStyle& style) {
  PanelShape shape;
  const PxRect rect = toPixels(c, designBox);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  shape.width = width;
  shape.height = height;
  if (width <= 0.0F || height <= 0.0F) return shape;
  const float stroke = c.dp(style.strokeWidthDp);
  const float inset = stroke * 0.5F;
  const float skew = std::min(std::tan(style.skewDegrees * kDegreesToRadians) * height,
      width * 0.42F);
  shape.skew = skew;
  const float topLeft = (style.topWider || style.straightLeft) ? inset : skew + inset;
  const float bottomLeft = (style.topWider && !style.straightLeft) ? skew + inset : inset;
  const float topRight = width - inset;
  const float bottomRight = style.straightRight ? width - inset : width - skew - inset;
  shape.points[0] = ImVec2(rect.min.x + topLeft, rect.min.y + inset);
  shape.points[1] = ImVec2(rect.min.x + topRight, rect.min.y + inset);
  shape.points[2] = ImVec2(rect.min.x + bottomRight, rect.min.y + height - inset);
  shape.points[3] = ImVec2(rect.min.x + bottomLeft, rect.min.y + height - inset);
  return shape;
}

void drawAccent(ImDrawList* drawList, const Canvas& c, const PanelShape& shape,
    const PanelStyle& style) {
  if (style.accentEdge == 0 || (style.accent & IM_COL32_A_MASK) == 0) return;
  const float width = std::max(1.0F, c.dp(style.accentWidthDp));
  const float w = shape.width;
  const float h = shape.height;
  if (w <= 0.0F || h <= 0.0F) return;
  const ImVec2 base = shape.points[3];
  const float originX = base.x - (style.straightLeft ? 0.0F : 0.0F);
  (void)originX;
  // The shape points are already in screen space; the origin is the panel's top-left
  // corner, which is the leftmost point of the bottom edge for the default variant.
  const float left = shape.points[3].x;
  const float top = shape.points[0].y;
  const float skew = shape.skew;
  ImVec2 accent[4];
  if (style.accentEdge == 1) {
    if (style.topWider && !style.straightLeft) {
      accent[0] = ImVec2(left, top);
      accent[1] = ImVec2(left + width, top);
      accent[2] = ImVec2(left + skew + width, top + h);
      accent[3] = ImVec2(left + skew, top + h);
    } else {
      const float topX = style.straightLeft ? 0.0F : skew;
      accent[0] = ImVec2(left + topX, top);
      accent[1] = ImVec2(left + topX + width, top);
      accent[2] = ImVec2(left + width, top + h);
      accent[3] = ImVec2(left, top + h);
    }
  } else if (style.accentEdge == 2) {
    const float bottomX = style.straightRight ? w : w - skew;
    accent[0] = ImVec2(left + w - width, top);
    accent[1] = ImVec2(left + w, top);
    accent[2] = ImVec2(left + bottomX, top + h);
    accent[3] = ImVec2(left + bottomX - width, top + h);
  } else {
    return;
  }
  drawList->AddConvexPolyFilled(accent, 4, style.accent);
}

float measureRun(std::string_view text, float pixelSize, bool bold) {
  UiFontCache& fonts = fontCache();
  const ImVec2 scale = framebufferScale();
  pixelSize *= scale.x;
  float width = 0.0F;
  std::uint32_t previous = 0;
  std::size_t index = 0;
  while (index < text.size()) {
    std::size_t length = 0;
    const std::uint32_t codepoint = UiFontCache::decode(text, index, length);
    index += length;
    if (length == 0) break;
    if (codepoint == '\n') break;
    if (previous != 0) width += fonts.kerning(previous, codepoint, pixelSize, bold) / scale.x;
    width += fonts.advance(codepoint, pixelSize, bold) / scale.x;
    previous = codepoint;
  }
  return width;
}

void drawRun(ImDrawList* drawList, std::string_view text, float x, float baseline, float pixelSize,
    bool bold, ImU32 color) {
  UiFontCache& fonts = fontCache();
  const std::uint64_t texture = fonts.textureId();
  if (texture == 0) return;
  const ImVec2 scale = framebufferScale();
  pixelSize *= scale.x;
  float pen = x;
  std::uint32_t previous = 0;
  std::size_t index = 0;
  while (index < text.size()) {
    std::size_t length = 0;
    const std::uint32_t codepoint = UiFontCache::decode(text, index, length);
    index += length;
    if (length == 0) break;
    if (codepoint == '\n') break;
    if (previous != 0) pen += fonts.kerning(previous, codepoint, pixelSize, bold) / scale.x;
    if (codepoint != ' ') {
      const UiFontCache::Glyph* glyph = fonts.glyph(codepoint, pixelSize, bold);
      if (glyph != nullptr && glyph->width > 0.0F) {
        // Snapping in device pixels avoids both a half-texel offset at 1:1 and magnification
        // of an atlas rasterised at logical size on a high-density framebuffer.
        const ImVec2 min(std::round((pen + glyph->offsetX / scale.x) * scale.x) / scale.x,
            std::round((baseline + glyph->offsetY / scale.y) * scale.y) / scale.y);
        const ImVec2 max(std::round((pen + (glyph->offsetX + glyph->width) / scale.x)
                * scale.x) / scale.x,
            std::round((baseline + (glyph->offsetY + glyph->height) / scale.y)
                * scale.y) / scale.y);
        drawList->AddImage(static_cast<ImTextureID>(texture), min, max,
            ImVec2(glyph->u0, glyph->v0), ImVec2(glyph->u1, glyph->v1), color);
      }
    }
    pen += fonts.advance(codepoint, pixelSize, bold) / scale.x;
    previous = codepoint;
  }
}

// Truncates `text` so that it plus an ellipsis fits `available` pixels.
std::string_view truncateWithEllipsis(std::string_view text, float pixelSize, bool bold,
    float available) {
  const float ellipsis = measureRun("...", pixelSize, bold);
  if (ellipsis >= available) return {};
  std::size_t lastFit = 0;
  std::size_t index = 0;
  float width = 0.0F;
  while (index < text.size()) {
    std::size_t length = 0;
    const std::uint32_t codepoint = UiFontCache::decode(text, index, length);
    const float advance = fontCache().advance(
        codepoint, pixelSize * framebufferScale().x, bold) / framebufferScale().x;
    if (width + advance + ellipsis > available) break;
    width += advance;
    index += length;
    lastFit = index;
  }
  return text.substr(0, lastFit);
}

// Fallback drawing used before the glyph cache exists (headless screen construction):
// the regular ImGui font keeps the vertex stream populated so a screen still draws.
void drawFallback(ImDrawList* drawList, ImVec2 topLeft, float pixelSize, ImU32 color,
    std::string_view text, ImVec2* extent) {
  ImFont* font = ImGui::GetFont();
  if (font == nullptr) {
    if (extent != nullptr) *extent = ImVec2(0.0F, pixelSize);
    return;
  }
  std::string owned(text);
  const ImVec2 measured = font->CalcTextSizeA(pixelSize, 4096.0F, 0.0F, owned.c_str());
  if (extent != nullptr) *extent = measured;
  drawList->AddText(font, pixelSize, topLeft, color, owned.c_str());
}

} // namespace

namespace {
// Read once per frame by every dp() conversion; a plain float is enough because the value
// only changes when the user moves the slider, never mid-frame.
float gInterfaceScale = 1.0F;
} // namespace

void setInterfaceScale(float scale) {
  if (!std::isfinite(scale) || scale <= 0.0F) return;
  gInterfaceScale = std::clamp(scale, 0.85F, 2.0F);
}

float interfaceScale() { return gInterfaceScale; }

namespace {
// Page announced by the frame being drawn, and what became of the press seen on it. The press
// keeps its verdict across the release frame so the control that ends the gesture can ask
// whether the gesture started here.
enum class PageGesture { Idle, Owned, Foreign };
int gPageId = -1;
PageGesture gPageGesture = PageGesture::Idle;
} // namespace

void beginPage(int pageId) {
  const bool switched = pageId != gPageId;
  gPageId = pageId;
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    // A click that arrives on the very frame the page appears was aimed at whatever was on
    // screen when the finger went down, which is not this page.
    gPageGesture = switched ? PageGesture::Foreign : PageGesture::Owned;
    return;
  }
  if (switched
      && (ImGui::IsMouseDown(ImGuiMouseButton_Left)
          || ImGui::IsMouseReleased(ImGuiMouseButton_Left))) {
    gPageGesture = PageGesture::Foreign;
    return;
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)
      && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    gPageGesture = PageGesture::Idle;
  }
}

bool pagePressOwned() { return gPageGesture == PageGesture::Owned; }

void cancelPagePress() {
  gPageGesture = PageGesture::Foreign;
  // The next frame always counts as a page change, so the foreign verdict survives until the
  // pointer is released even if the page keeps its identity.
  gPageId = -1;
}

Canvas canvas() {
  Canvas result;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  result.viewport = viewport->WorkSize;
  if (result.viewport.x <= 1.0F || result.viewport.y <= 1.0F) {
    result.viewport = ImVec2(1280.0F, 720.0F);
  }
  result.scale = std::min(result.viewport.x / kDesignWidth, result.viewport.y / kDesignHeight);
  result.scale = std::max(0.01F, result.scale);
  result.origin = ImVec2(result.viewport.x * 0.5F - kDesignWidth * result.scale * 0.5F,
      result.viewport.y * 0.5F - kDesignHeight * result.scale * 0.5F);
  return result;
}

PanelStyle glassPanel(ImU32 fill, ImU32 stroke) {
  PanelStyle style;
  style.fill = fill;
  style.stroke = stroke;
  style.strokeWidthDp = 1.0F;
  style.skewDegrees = 15.0F;
  return style;
}

PanelStyle rectPanel(ImU32 fill, ImU32 stroke) {
  PanelStyle style = glassPanel(fill, stroke);
  style.skewDegrees = 0.0F;
  return style;
}

void drawPanelShape(ImDrawList* drawList, const Canvas& c, const Box& designBox,
    const PanelStyle& style) {
  if (drawList == nullptr) return;
  const PanelShape shape = panelShape(c, designBox, style);
  if (shape.width <= 0.0F || shape.height <= 0.0F) return;
  drawList->AddConvexPolyFilled(shape.points, 4, style.fill);
  const float stroke = c.dp(style.strokeWidthDp);
  if (stroke > 0.0F && (style.stroke & IM_COL32_A_MASK) != 0) {
    drawList->AddPolyline(shape.points, 4, style.stroke, ImDrawFlags_Closed, stroke);
  }
  drawAccent(drawList, c, shape, style);
}

bool panelContains(const Canvas& c, const Box& designBox, const PanelStyle& style,
    ImVec2 screenPoint) {
  const PxRect rect = toPixels(c, designBox);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  if (width <= 0.0F || height <= 0.0F) return false;
  const float localX = screenPoint.x - rect.min.x;
  const float localY = screenPoint.y - rect.min.y;
  if (localY < 0.0F || localY > height) return false;
  const float skew = std::min(std::tan(style.skewDegrees * kDegreesToRadians) * height,
      width * 0.42F);
  const float t = localY / height;
  const float left = style.straightLeft ? 0.0F : (style.topWider ? skew * t : skew * (1.0F - t));
  const float right = style.straightRight ? width : width - skew * t;
  return localX >= left && localX <= right;
}

Interaction panelInteraction(const char* id, const Canvas& c, const Box& designBox,
    const PanelStyle& style) {
  Interaction result;
  const PxRect rect = toPixels(c, designBox);
  ImGui::SetCursorScreenPos(rect.min);
  const bool released = ImGui::InvisibleButton(id, ImVec2(rect.max.x - rect.min.x,
      rect.max.y - rect.min.y));
  const ImGuiIO& io = ImGui::GetIO();
  result.hovered = ImGui::IsItemHovered()
      && panelContains(c, designBox, style, io.MousePos);
  // The release only counts when the press that preceded it started on this plate as well, so
  // a gesture that began on the backdrop, on another control, or on the page that was up
  // before this one cannot press it.
  result.pressed = released && result.hovered
      && panelContains(c, designBox, style, io.MouseClickedPos[ImGuiMouseButton_Left]);
  return result;
}

float labelWidth(const Canvas& c, std::string_view text, float sizeDp, bool bold) {
  const float pixelSize = c.dp(sizeDp);
  if (!fontCache().ready()) {
    ImFont* font = ImGui::GetFont();
    if (font == nullptr) return 0.0F;
    std::string owned(text);
    return font->CalcTextSizeA(pixelSize, 4096.0F, 0.0F, owned.c_str()).x / c.scale;
  }
  return measureRun(text, pixelSize, bold) / c.scale;
}

float labelLineHeight(const Canvas& c, float sizeDp) {
  const float pixelSize = c.dp(sizeDp);
  if (!fontCache().ready()) return pixelSize / c.scale;
  const ImVec2 scale = framebufferScale();
  return (fontCache().ascent(pixelSize * scale.x) + fontCache().descent(pixelSize * scale.x))
      / (scale.y * c.scale);
}

void drawLabelLine(ImDrawList* drawList, const Canvas& c, ImVec2 designTopLeft,
    std::string_view text, float sizeDp, bool bold, ImU32 color, ImVec2* measured) {
  const float pixelSize = c.dp(sizeDp);
  const ImVec2 origin = c.point(designTopLeft.x, designTopLeft.y);
  const ImVec2 scale = framebufferScale();
  if (!fontCache().ready()) {
    ImVec2 extent;
    drawFallback(drawList, origin, pixelSize, color, text, &extent);
    if (measured != nullptr) {
      measured->x = extent.x / c.scale;
      measured->y = extent.y / c.scale;
    }
    return;
  }
  UiFontCache& fonts = fontCache();
  const float ascent = fonts.ascent(pixelSize * scale.x) / scale.y;
  const float descent = fonts.descent(pixelSize * scale.x) / scale.y;
  drawRun(drawList, text, origin.x, origin.y + ascent, pixelSize, bold, color);
  if (measured != nullptr) {
    measured->x = measureRun(text, pixelSize, bold) / c.scale;
    measured->y = (ascent + descent) / c.scale;
  }
}

float drawLabel(ImDrawList* drawList, const Canvas& c, const Box& designBox, std::string_view text,
    const TextStyle& style, double nowSec) {
  if (drawList == nullptr || text.empty()) return 0.0F;
  const PxRect rect = toPixels(c, designBox);
  const float boxWidth = rect.max.x - rect.min.x;
  const float boxHeight = rect.max.y - rect.min.y;
  if (boxWidth <= 0.0F || boxHeight <= 0.0F) return 0.0F;
  const float paddingLeft = c.dp(style.paddingLeftDp);
  const float paddingRight = c.dp(style.paddingRightDp);
  const float available = std::max(1.0F, boxWidth - paddingLeft - paddingRight);
  float pixelSize = c.dp(style.sizeDp);
  if (!(pixelSize > 0.0F)) return 0.0F;

  if (!fontCache().ready()) {
    const ImVec2 origin(rect.min.x + paddingLeft, rect.min.y);
    ImVec2 extent;
    drawFallback(drawList, origin, pixelSize, style.color, text, &extent);
    return extent.x / c.scale;
  }

  UiFontCache& fonts = fontCache();
  float textWidth = measureRun(text, pixelSize, style.bold);
  const ImVec2 scale = framebufferScale();
  const float ascent = fonts.ascent(pixelSize * scale.x) / scale.y;
  const float descent = fonts.descent(pixelSize * scale.x) / scale.y;

  // Fit-to-width shrinks the text so a fixed box never clips it, matching the reference
  // label behaviour. Ellipsis truncates instead, which long chart names use.
  const bool marquee = style.marquee && textWidth > available;
  if (!marquee) {
    if (style.ellipsize && textWidth > available) {
      text = truncateWithEllipsis(text, pixelSize, style.bold, available);
      textWidth = measureRun(text, pixelSize, style.bold);
    } else if (style.fitWidth && textWidth > available) {
      // Bisect for the largest size that still fits, exactly as the reference does: twelve
      // halvings stop at 0.05 px, which is below the rasteriser's own precision. A fixed
      // number of proportional steps was not enough to converge — a label whose width
      // shrinks non-linearly with size settled below the size that would have fit, which is
      // why fitted text came out visibly smaller than the same label on the reference.
      float low = 0.01F;
      float high = pixelSize;
      for (int iteration = 0; iteration < 12 && high - low > 0.05F; ++iteration) {
        const float middle = (low + high) * 0.5F;
        if (measureRun(text, middle, style.bold) <= available) {
          low = middle;
        } else {
          high = middle;
        }
      }
      pixelSize = low;
      textWidth = measureRun(text, pixelSize, style.bold);
    }
  }

  float x = rect.min.x + paddingLeft;
  if (style.align == HAlign::Center) {
    x = rect.min.x + boxWidth * 0.5F - textWidth * 0.5F;
  } else if (style.align == HAlign::Right) {
    x = rect.max.x - paddingRight - textWidth;
  }
  float baseline = rect.min.y + (ascent + descent) * 0.5F;
  if (style.valign == VAlign::Top) {
    baseline = rect.min.y + ascent;
  } else if (style.valign == VAlign::Middle) {
    baseline = rect.min.y + boxHeight * 0.5F - (ascent + descent) * 0.5F + ascent;
  } else if (style.valign == VAlign::Bottom) {
    baseline = rect.max.y - descent;
  }

  drawList->PushClipRect(rect.min, rect.max, true);
  if (marquee) {
    const float travel = textWidth + c.dp(40.0F);
    const float speed = c.dp(30.0F);
    const float elapsed = static_cast<float>(nowSec) - 1.0F;
    float offset = 0.0F;
    if (elapsed > 0.0F) {
      offset = std::fmod(elapsed * speed, travel);
      if (offset < 0.0F) offset = 0.0F;
    }
    const float startX = rect.min.x + paddingLeft - offset;
    drawRun(drawList, text, startX, baseline, pixelSize, style.bold, style.color);
    drawRun(drawList, text, startX + travel, baseline, pixelSize, style.bold, style.color);
  } else {
    drawRun(drawList, text, x, baseline, pixelSize, style.bold, style.color);
  }
  drawList->PopClipRect();
  return textWidth / c.scale;
}

void drawImage(ImDrawList* drawList, const PxRect& box, std::uint64_t texture, int textureWidth,
    int textureHeight, ImageFit fit, ImU32 tint) {
  if (drawList == nullptr || texture == 0 || textureWidth <= 0 || textureHeight <= 0) return;
  const float boxWidth = box.max.x - box.min.x;
  const float boxHeight = box.max.y - box.min.y;
  if (boxWidth <= 0.0F || boxHeight <= 0.0F) return;
  const float textureAspect = static_cast<float>(textureWidth) / static_cast<float>(textureHeight);
  const float boxAspect = boxWidth / boxHeight;
  ImVec2 min = box.min;
  ImVec2 max = box.max;
  bool clip = false;
  switch (fit) {
    case ImageFit::Fill:
      break;
    case ImageFit::FitCenter: {
      float width = boxWidth;
      float height = boxWidth / textureAspect;
      if (height > boxHeight) {
        height = boxHeight;
        width = boxHeight * textureAspect;
      }
      min = ImVec2(box.min.x + (boxWidth - width) * 0.5F,
          box.min.y + (boxHeight - height) * 0.5F);
      max = ImVec2(min.x + width, min.y + height);
      break;
    }
    case ImageFit::CenterCrop: {
      float width = boxWidth;
      float height = boxWidth / textureAspect;
      if (height < boxHeight) {
        height = boxHeight;
        width = boxHeight * textureAspect;
      }
      min = ImVec2(box.min.x + (boxWidth - width) * 0.5F,
          box.min.y + (boxHeight - height) * 0.5F);
      max = ImVec2(min.x + width, min.y + height);
      clip = true;
      break;
    }
    case ImageFit::Center: {
      (void)boxAspect;
      min = ImVec2(box.min.x + (boxWidth - static_cast<float>(textureWidth)) * 0.5F,
          box.min.y + (boxHeight - static_cast<float>(textureHeight)) * 0.5F);
      max = ImVec2(min.x + static_cast<float>(textureWidth),
          min.y + static_cast<float>(textureHeight));
      break;
    }
  }
  if (clip) drawList->PushClipRect(box.min, box.max, true);
  drawList->AddImage(static_cast<ImTextureID>(texture), min, max, ImVec2(0.0F, 0.0F),
      ImVec2(1.0F, 1.0F), tint);
  if (clip) drawList->PopClipRect();
}

void drawIconPlate(ImDrawList* drawList, const Canvas& c, const Box& designBox,
    const PanelStyle& style, std::uint64_t texture, int textureWidth, int textureHeight,
    float paddingXDp, float paddingYDp, float translateXDp, ImU32 tint) {
  drawPanelShape(drawList, c, designBox, style);
  if (texture == 0) return;
  const PxRect rect = toPixels(c, designBox);
  const float paddingX = c.dp(paddingXDp);
  const float paddingY = c.dp(paddingYDp);
  PxRect content;
  content.min = ImVec2(rect.min.x + paddingX, rect.min.y + paddingY);
  content.max = ImVec2(rect.max.x - paddingX, rect.max.y - paddingY);
  content.min.x += c.dp(translateXDp);
  content.max.x += c.dp(translateXDp);
  if (content.max.x <= content.min.x || content.max.y <= content.min.y) return;
  drawImage(drawList, content, texture, textureWidth, textureHeight, ImageFit::FitCenter, tint);
}

float interpolate(Interp interp, float t) {
  t = std::clamp(t, 0.0F, 1.0F);
  switch (interp) {
    case Interp::Linear:
      return t;
    case Interp::AccelerateQuad:
      return t * t;
    case Interp::DecelerateQuad: {
      const float inverse = 1.0F - t;
      return 1.0F - inverse * inverse;
    }
    case Interp::AccelerateDecelerate:
      return (std::cos((t + 1.0F) * kPi) * 0.5F) + 0.5F;
    case Interp::Overshoot: {
      const float shifted = t - 1.0F;
      return shifted * shifted * (3.0F * shifted + 2.0F) + 1.0F;
    }
    case Interp::EaseOutCubic:
    case Interp::DecelerateCubic: {
      const float inverse = 1.0F - t;
      return 1.0F - inverse * inverse * inverse;
    }
  }
  return t;
}

float progress(double startSec, float durationSec, Interp interp, double nowSec) {
  if (startSec <= 0.0 || durationSec <= 0.0F) return 1.0F;
  const float t = static_cast<float>((nowSec - startSec) / static_cast<double>(durationSec));
  if (t >= 1.0F) return 1.0F;
  return interpolate(interp, std::max(0.0F, t));
}

void drawTabBar(ImDrawList* drawList, const Canvas& c, const Box& designBox,
    const TabBarStyle& style, double nowSec) {
  if (drawList == nullptr || style.labels == nullptr || style.count <= 0) return;
  const PxRect rect = toPixels(c, designBox);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  if (width <= 0.0F || height <= 0.0F) return;

  const float barTop = height * 0.14F;
  const float barBottom = height * 0.86F;
  const float skew = std::tan(15.0F * kDegreesToRadians) * (barBottom - barTop);

  const auto skewRect = [&drawList, &rect](float leftTop, float top, float rightTop, float bottom,
      float skewPixels, ImU32 color) {
    ImVec2 points[4];
    points[0] = ImVec2(rect.min.x + leftTop, rect.min.y + top);
    points[1] = ImVec2(rect.min.x + rightTop, rect.min.y + top);
    points[2] = ImVec2(rect.min.x + rightTop - skewPixels, rect.min.y + bottom);
    points[3] = ImVec2(rect.min.x + leftTop - skewPixels, rect.min.y + bottom);
    drawList->AddConvexPolyFilled(points, 4, color);
  };

  skewRect(width * 0.055F + skew, barTop, width * 0.945F, barBottom, skew, IM_COL32(5, 7, 11, 178));

  std::vector<float> centers(static_cast<std::size_t>(style.count));
  std::vector<float> widths(static_cast<std::size_t>(style.count));
  for (int index = 0; index < style.count; ++index) {
    centers[static_cast<std::size_t>(index)] =
        0.08F + (static_cast<float>(index) + 0.5F) * 0.84F / static_cast<float>(style.count);
    widths[static_cast<std::size_t>(index)] =
        0.84F / static_cast<float>(style.count);
  }
  if (style.count == 3) {
    centers[0] = 0.22F;
    centers[1] = 0.50F;
    centers[2] = 0.78F;
    widths[0] = 0.23F;
    widths[1] = 0.23F;
    widths[2] = 0.29F;
  }

  const float p = progress(style.startSec, 0.34F, Interp::EaseOutCubic, nowSec);
  const bool moving = style.from != style.to && style.startSec > 0.0;
  const int fromIndex = std::clamp(style.from, 0, style.count - 1);
  const int toIndex = std::clamp(style.to, 0, style.count - 1);
  const float selectedCenter = centers[static_cast<std::size_t>(fromIndex)]
      + (centers[static_cast<std::size_t>(toIndex)]
            - centers[static_cast<std::size_t>(fromIndex)])
          * p;
  const float selectedWidth = widths[static_cast<std::size_t>(fromIndex)]
      + (widths[static_cast<std::size_t>(toIndex)] - widths[static_cast<std::size_t>(fromIndex)])
          * p;
  skewRect((selectedCenter - selectedWidth * 0.5F) * width + skew, 0.0F,
      (selectedCenter + selectedWidth * 0.5F) * width, height, skew, IM_COL32(232, 234, 238, 255));

  const float labelSizePx = c.dp(18.0F);
  for (int index = 0; index < style.count; ++index) {
    float selectedAmount = 0.0F;
    if (!moving) {
      selectedAmount = index == toIndex ? 1.0F : 0.0F;
    } else if (index == fromIndex) {
      selectedAmount = 1.0F - p;
    } else if (index == toIndex) {
      selectedAmount = p;
    }
    const int tone = static_cast<int>(std::lround(255.0F * (1.0F - selectedAmount)));
    const ImU32 color = IM_COL32(tone, tone, tone, 255);
    const float centerX = rect.min.x + centers[static_cast<std::size_t>(index)] * width;
    const float available = std::max(
        1.0F, std::round(widths[static_cast<std::size_t>(index)] * width - skew - c.dp(8.0F)));
    std::string_view label = style.labels[index] != nullptr ? style.labels[index] : "";
    float labelPx = labelSizePx;
    float labelAdvance = fontCache().ready()
        ? measureRun(label, labelPx, true)
        : ImGui::CalcTextSize(label.data(), label.data() + label.size()).x;
    if (fontCache().ready() && labelAdvance > available) {
      labelPx = std::max(1.0F, labelPx * available / labelAdvance);
      labelAdvance = measureRun(label, labelPx, true);
    }
    const float lineHeight = fontCache().ready()
        ? fontCache().ascent(labelPx) + fontCache().descent(labelPx)
        : labelPx;
    const float top = rect.min.y + (height - lineHeight) * 0.5F;
    if (fontCache().ready()) {
      drawRun(drawList, label, centerX - labelAdvance * 0.5F,
          top + fontCache().ascent(labelPx), labelPx, true, color);
    } else {
      ImVec2 extent;
      drawFallback(drawList, ImVec2(centerX - labelAdvance * 0.5F, top), labelPx, color, label,
          &extent);
    }
  }
}

int hitTabBar(const Canvas& c, const Box& designBox, ImVec2 screenPoint, int count) {
  if (count <= 0) return -1;
  const PxRect rect = toPixels(c, designBox);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  if (width <= 0.0F || height <= 0.0F) return -1;
  if (screenPoint.x < rect.min.x || screenPoint.x > rect.max.x || screenPoint.y < rect.min.y
      || screenPoint.y > rect.max.y) {
    return -1;
  }
  const float slice = std::max(1.0F, width / static_cast<float>(count));
  const int index = static_cast<int>((screenPoint.x - rect.min.x) / slice);
  return std::clamp(index, 0, count - 1);
}

} // namespace rechigros::app::ui
