#include "rechigros/app/Screens.hpp"

#include "rechigros/app/AppContext.hpp"
#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiText.hpp"
#include "rechigros/platform/FileDialog.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace rechigros::app {
namespace {

// The page is laid out on a fixed design canvas so every control keeps its position at any
// window size, which is what makes a settings list readable rather than reflowing per resize.
constexpr float kRowHeight = 30.0F;
constexpr float kLabelWidth = 150.0F;
constexpr float kValueWidth = 210.0F;
constexpr int kCodecCount = 3;

struct Layout {
  float left{};
  float right{};
  float top{};
};

Layout pageLayout(const ui::Canvas& canvas) {
  // A debug-console style column: everything sits in one stack with the label on the left and
  // the current value on the right, so the values scan vertically.
  Layout layout;
  layout.left = 24.0F;
  layout.right = canvas.viewport.x - 24.0F;
  layout.top = 26.0F;
  return layout;
}

std::string fpsText(int fps) { return std::to_string(fps) + " fps"; }

const char* codecLabel(VideoEncoder::Codec codec) {
  switch (codec) {
    case VideoEncoder::Codec::H265: return "H.265";
    case VideoEncoder::Codec::Vp9: return "VP9";
    case VideoEncoder::Codec::H264:
    default: return "H.264";
  }
}

// Current recording settings live on the context so leaving the page and coming back keeps them,
// which is what a user expects from a settings screen.
RecordSession::Settings& settingsState(AppContext& context) {
  static RecordSession::Settings state;
  if (state.fileName.empty()) {
    state.outputDirectory = RecordSession::defaultOutputDirectory();
    state.fileName = RecordSession::defaultFileName();
  }
  if (state.outputDirectory.empty()) state.outputDirectory = RecordSession::defaultOutputDirectory();
  return state;
}

// The action the start plate performs: load the chart the page describes if it is not loaded, then
// begin the offscreen run. Both failures are reported to the player, because a render that never
// starts looks like a dead button.
void startRender(AppContext& context, const RecordSession::Settings& settings) {
  std::string error;
  if (!context.game.loaded || context.game.chart == nullptr) {
    if (!prepareRenderChart(context, error)) {
      notify(context, error.empty() ? "无法开始渲染" : error);
      return;
    }
  }
  if (!context.recording.start(context, settings, error)) {
    notify(context, error.empty() ? "无法开始渲染" : error);
  }
}

void drawRowLabel(ImDrawList* drawList, const ui::Canvas& canvas, const Layout& layout, int row,
    std::string_view text) {
  ui::TextStyle style;
  style.sizeDp = 13.0F;
  style.color = ui::rgba(0xFFCBD5E1U);
  const float y = layout.top + static_cast<float>(row) * kRowHeight;
  ui::drawLabel(drawList, canvas,
      ui::Box{layout.left, y, layout.left + kLabelWidth, y + kRowHeight - 4.0F}, text, style,
      ImGui::GetTime());
}

// The plate behind a value: a dark field with a hairline border, which is what makes the column read
// as a console list. Shared so an editable row and a clickable row look identical.
void drawValuePlate(const ui::Canvas& canvas, const ui::Box& box, bool highlighted) {
  const ImU32 fill = highlighted ? ui::rgba(0xFF1E3A5FU) : ui::rgba(0xFF161B22U);
  ImGui::GetWindowDrawList()->AddRectFilled(canvas.point(box.x0, box.y0),
      canvas.point(box.x1, box.y1), fill, 3.0F);
  ImGui::GetWindowDrawList()->AddRect(canvas.point(box.x0, box.y0), canvas.point(box.x1, box.y1),
      ui::rgba(0xFF30363DU), 3.0F, 0, 1.0F);
}

// A value cell that looks like a console field: a dark plate with the value inside. Returns the
// interaction so callers can decide what a press does.
ui::Interaction valueCell(const char* id, const ui::Canvas& canvas, const Layout& layout, int row,
    std::string_view value, bool highlighted) {
  const float y = layout.top + static_cast<float>(row) * kRowHeight;
  const ui::Box box{layout.left + kLabelWidth, y, layout.left + kLabelWidth + kValueWidth,
      y + kRowHeight - 4.0F};
  drawValuePlate(canvas, box, highlighted);
  ui::TextStyle style;
  style.sizeDp = 12.5F;
  style.color = ui::rgba(0xFFE6EDF3U);
  style.paddingLeftDp = 6.0F;
  style.paddingRightDp = 6.0F;
  style.ellipsize = true;
  ui::drawLabel(ImGui::GetWindowDrawList(), canvas, box, value, style, ImGui::GetTime());
  return ui::panelInteraction(id, canvas, box, ui::rectPanel(0x00000000U, 0x00000000U));
}

} // namespace

void drawRenderScreen(AppContext& context) {
  const ui::Canvas canvas = ui::canvas();
  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  RecordSession::Settings& settings = settingsState(context);
  const auto& resolutions = RecordSession::resolutions();
  const bool busy = context.recording.state() == RecordSession::State::Running;

  ui::beginFullscreenWindow("##render");
  drawList = ImGui::GetWindowDrawList();

  Layout layout = pageLayout(canvas);
  drawRowLabel(drawList, canvas, layout, 0, "渲染设置");

  // The list is a flat console: one labelled row per parameter, in the order they are decided.
  int row = 1;
  int nextResolution = settings.resolutionIndex;
  if (valueCell("##render_resolution", canvas, layout, row,
          resolutions[static_cast<std::size_t>(
              std::clamp(settings.resolutionIndex, 0,
                  static_cast<int>(resolutions.size()) - 1))].label,
          false)
          .pressed
      && !busy) {
    nextResolution = (settings.resolutionIndex + 1) % static_cast<int>(resolutions.size());
  }
  drawRowLabel(drawList, canvas, layout, row, "分辨率");
  settings.resolutionIndex = std::clamp(nextResolution, 0,
      static_cast<int>(resolutions.size()) - 1);
  ++row;

  int nextFps = settings.fps;
  if (valueCell("##render_fps", canvas, layout, row, fpsText(settings.fps), false).pressed
      && !busy) {
    // The offered rates are the common recording rates; below 30 the motion of a falling note is
    // no longer smooth enough to read.
    static const int kRates[] = {30, 60, 120, 144, 240};
    int index = 0;
    for (int candidate = 0; candidate < 5; ++candidate) {
      if (kRates[candidate] == settings.fps) index = candidate;
    }
    nextFps = kRates[(index + 1) % 5];
  }
  drawRowLabel(drawList, canvas, layout, row, "帧率");
  settings.fps = std::clamp(nextFps, 15, 240);
  ++row;

  int nextCodec = static_cast<int>(settings.codec);
  if (valueCell("##render_codec", canvas, layout, row, codecLabel(settings.codec), false).pressed
      && !busy) {
    nextCodec = (nextCodec + 1) % kCodecCount;
  }
  drawRowLabel(drawList, canvas, layout, row, "编码器");
  settings.codec = static_cast<VideoEncoder::Codec>(std::clamp(nextCodec, 0, kCodecCount - 1));
  ++row;

  // The file name and the directory are typed values, so they are edited elsewhere; here the row
  // shows the current choice and accepts a press to raise the picker.
  if (valueCell("##render_output_dir", canvas, layout, row,
          platform::pathToUtf8(settings.outputDirectory), false)
          .pressed
      && !busy) {
    const auto chosen = platform::FileDialog::pickDirectory();
    if (!chosen.empty()) settings.outputDirectory = chosen;
  }
  drawRowLabel(drawList, canvas, layout, row, "输出目录");
  ++row;

  // The file name is typed directly. The field sits over the value plate with its own colours
  // suppressed, so the plate and the text are drawn by the same code as every other row instead of
  // the input widget introducing a second visual style.
  {
    const float y = layout.top + static_cast<float>(row) * kRowHeight;
    const ui::Box box{layout.left + kLabelWidth, y, layout.left + kLabelWidth + kValueWidth,
        y + kRowHeight - 4.0F};
    drawValuePlate(canvas, box, false);
    ui::TextStyle style;
    style.sizeDp = 12.5F;
    style.color = ui::rgba(0xFFE6EDF3U);
    style.paddingLeftDp = 6.0F;
    style.paddingRightDp = 6.0F;
    if (busy) {
      ui::drawLabel(ImGui::GetWindowDrawList(), canvas, box, settings.fileName, style,
          ImGui::GetTime());
    } else {
      ImGui::SetCursorScreenPos(canvas.point(box.x0 + 4.0F, box.y0 + 1.0F));
      ImGui::SetNextItemWidth(canvas.units(box.width() - 8.0F));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.902F, 0.929F, 0.953F, 1.0F));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
          ImVec2(0.0F, std::max(0.0F, (canvas.units(box.height() - 4.0F) - ImGui::GetFontSize())
                      * 0.5F)));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
      char buffer[256];
      std::snprintf(buffer, sizeof(buffer), "%s", settings.fileName.c_str());
      if (ImGui::InputText("##render_file_name", buffer, sizeof(buffer))) {
        settings.fileName = buffer;
      }
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(3);
      // An empty name would produce a file called ".mp4", so the field falls back to a fresh
      // timestamp as soon as it loses focus.
      if (settings.fileName.empty()) settings.fileName = RecordSession::defaultFileName();
    }
  }
  drawRowLabel(drawList, canvas, layout, row, "文件名");
  ++row;

  // The combo label is typed like the file name: the HUD position prints whatever word the
  // recording should carry, and a fixed list of four choices cannot express that.
  {
    const float y = layout.top + static_cast<float>(row) * kRowHeight;
    const ui::Box box{layout.left + kLabelWidth, y, layout.left + kLabelWidth + kValueWidth,
        y + kRowHeight - 4.0F};
    drawValuePlate(canvas, box, false);
    if (busy) {
      ui::TextStyle style;
      style.sizeDp = 12.5F;
      style.color = ui::rgba(0xFFE6EDF3U);
      style.paddingLeftDp = 6.0F;
      style.paddingRightDp = 6.0F;
      ui::drawLabel(ImGui::GetWindowDrawList(), canvas, box, settings.comboLabel, style,
          ImGui::GetTime());
    } else {
      ImGui::SetCursorScreenPos(canvas.point(box.x0 + 4.0F, box.y0 + 1.0F));
      ImGui::SetNextItemWidth(canvas.units(box.width() - 8.0F));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.902F, 0.929F, 0.953F, 1.0F));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
          ImVec2(0.0F, std::max(0.0F, (canvas.units(box.height() - 4.0F) - ImGui::GetFontSize())
                      * 0.5F)));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
      char buffer[64];
      std::snprintf(buffer, sizeof(buffer), "%s", settings.comboLabel.c_str());
      if (ImGui::InputText("##render_combo", buffer, sizeof(buffer))) {
        settings.comboLabel = buffer;
      }
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(3);
      // A run label is drawn in the HUD position, so an empty one leaves a blank readout and has
      // nothing to fall back on; the recorded run then carries the default word.
      if (settings.comboLabel.empty()) settings.comboLabel = "RECORD";
    }
  }
  drawRowLabel(drawList, canvas, layout, row, "COMBO 标签");
  ++row;

  if (valueCell("##render_hitsound", canvas, layout, row,
          settings.noteHitsound ? "开" : "关", settings.noteHitsound)
          .pressed
      && !busy) {
    settings.noteHitsound = !settings.noteHitsound;
  }
  drawRowLabel(drawList, canvas, layout, row, "打击音");
  ++row;

  if (valueCell("##render_particles", canvas, layout, row,
          settings.noteHitParticles ? "开" : "关", settings.noteHitParticles)
          .pressed
      && !busy) {
    settings.noteHitParticles = !settings.noteHitParticles;
  }
  drawRowLabel(drawList, canvas, layout, row, "打击粒子");
  ++row;

  // Actions at the bottom of the column, separated from the parameters.
  row += 1;
  const float actionY = layout.top + static_cast<float>(row) * kRowHeight;
  const ui::Box startBox{layout.left, actionY, layout.left + 160.0F, actionY + 34.0F};
  ImGui::GetWindowDrawList()->AddRectFilled(canvas.point(startBox.x0, startBox.y0),
      canvas.point(startBox.x1, startBox.y1),
      busy ? ui::rgba(0xFF24303FU) : ui::rgba(0xFF1F6FEBU), 4.0F);
  ui::TextStyle buttonStyle;
  buttonStyle.sizeDp = 14.0F;
  buttonStyle.bold = true;
  buttonStyle.align = ui::HAlign::Center;
  ui::drawLabel(ImGui::GetWindowDrawList(), canvas, startBox, busy ? "渲染中" : "开始渲染",
      buttonStyle, ImGui::GetTime());
  if (!busy && ui::panelInteraction("##render_start", canvas, startBox,
                        ui::rectPanel(0x00000000U, 0x00000000U))
                        .pressed) {
    startRender(context, settings);
  }

  const ui::Box backBox{startBox.x1 + 12.0F, actionY, startBox.x1 + 112.0F, actionY + 34.0F};
  ImGui::GetWindowDrawList()->AddRectFilled(canvas.point(backBox.x0, backBox.y0),
      canvas.point(backBox.x1, backBox.y1), ui::rgba(0xFF21262DU), 4.0F);
  ui::drawLabel(ImGui::GetWindowDrawList(), canvas, backBox, "返回", buttonStyle, ImGui::GetTime());
  if (ui::panelInteraction("##render_back", canvas, backBox,
          ui::rectPanel(0x00000000U, 0x00000000U))
          .pressed
      && !busy) {
    navigateTo(context, context.renderReturn);
  }

  ui::endFullscreenWindow();
}

bool updateRecording(AppContext& context) {
  auto& recording = context.recording;
  if (recording.state() == RecordSession::State::Running) {
    // The recording renders a bounded number of frames per call and returns whether more remain,
    // so the interface keeps drawing a progress bar instead of freezing for the whole video.
    if (recording.step(context)) return true;
    if (recording.state() == RecordSession::State::Finished) {
      // The path is shown in the interface, which draws UTF-8. A narrow path is in the local code
      // page, so joining it here would show a mangled name for any folder outside ASCII.
      notify(context, "渲染完成：" + platform::pathToUtf8(recording.outputPath()));
    } else if (recording.state() == RecordSession::State::Failed) {
      notify(context, "渲染失败：" + recording.error());
    }
    return false;
  }
  return false;
}

void drawRecordingProgress(AppContext& context) {
  auto& recording = context.recording;
  if (recording.state() != RecordSession::State::Running) return;
  // The overlay is authored in the same design units as every other screen and converted through
  // the canvas when drawn, so the panel stays centred whatever the window size or density is.
  const ui::Canvas canvas = ui::canvas();
  // The overlay owns a clickable control, and an immediate-mode widget has to belong to a window.
  // Submitting it with none open makes the toolkit create its own fallback window, which then
  // shows up as a stray panel over the recording.
  // The settings page owns focus when the run starts, so explicitly raise this window above it.
  ImGui::SetNextWindowFocus();
  ui::beginFullscreenWindow("##recording_progress", true);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  // The window rectangle in draw-list coordinates: the draw list spans the whole window, so this
  // does not go through the canvas at all.
  const ImVec2 windowSize = ImGui::GetIO().DisplaySize;
  // The full-screen window starts at the viewport's work position, which is not necessarily the
  // draw list origin, so the rectangle is measured against the window's own top-left.
  const ImVec2 windowOrigin = ImGui::GetWindowPos();
  const ui::PxRect full{windowOrigin,
      ImVec2(windowOrigin.x + windowSize.x, windowOrigin.y + windowSize.y)};
  drawList->AddRectFilled(full.min, full.max, ui::rgba(0xC0080B10U));

  constexpr float kPanelWidth = 420.0F;
  constexpr float kPanelHeight = 118.0F;
  constexpr float kPad = 20.0F;
  // The full-screen rectangle is already in draw-list pixels, so the panel is centred on it
  // directly; deriving the size from the canvas would mix the two spaces again.
  const float viewWidth = full.max.x - full.min.x;
  const float viewHeight = full.max.y - full.min.y;
  const float panelWidthPixels = canvas.units(kPanelWidth);
  const float panelHeightPixels = canvas.units(kPanelHeight);
  const float x0 = std::round(full.min.x + (viewWidth - panelWidthPixels) * 0.5F);
  const float y0 = std::round(full.min.y + (viewHeight - panelHeightPixels) * 0.5F);
  const auto fillPixels = [&](float bx, float by, float bw, float bh, std::uint32_t colour) {
    drawList->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), ui::rgba(colour), 4.0F);
  };
  const auto designBox = [&](float bx, float by, float bw, float bh) {
    return ui::Box{(bx - canvas.origin.x) / canvas.scale, (by - canvas.origin.y) / canvas.scale,
        (bx + bw - canvas.origin.x) / canvas.scale, (by + bh - canvas.origin.y) / canvas.scale};
  };
  const float panelWidth = canvas.units(kPanelWidth);
  const float panelHeight = canvas.units(kPanelHeight);
  const float pad = canvas.units(kPad);
  fillPixels(x0, y0, panelWidth, panelHeight, 0xFF161B22U);
  ui::TextStyle title;
  title.sizeDp = 15.0F;
  title.bold = true;
  ui::drawLabel(drawList, canvas,
      designBox(x0 + pad, y0 + canvas.units(14.0F), panelWidth - pad * 2.0F,
          canvas.units(26.0F)),
      "正在渲染", title, ImGui::GetTime());

  const float barY = y0 + canvas.units(50.0F);
  const float barHeight = canvas.units(16.0F);
  const float barLeft = x0 + pad;
  const float barRight = x0 + panelWidth - pad;
  fillPixels(barLeft, barY, barRight - barLeft, barHeight, 0xFF0D1117U);
  const bool preparingAudio = recording.preparingAudio();
  const float progress = preparingAudio ? recording.audioProgress() : recording.progress();
  const float filled = barLeft + (barRight - barLeft) * progress;
  if (filled > barLeft + 1.0F) {
    fillPixels(barLeft, barY, filled - barLeft, barHeight, 0xFF1F6FEBU);
  }

  char status[128];
  if (preparingAudio) {
    std::snprintf(status, sizeof(status), "正在编码音频  %d%%",
        static_cast<int>(progress * 100.0F));
  } else {
    std::snprintf(status, sizeof(status), "%d / %d 帧  %d%%", recording.renderedFrames(),
        recording.totalFrames(), static_cast<int>(progress * 100.0F));
  }
  ui::TextStyle detail;
  detail.sizeDp = 12.0F;
  detail.color = ui::rgba(0xFF8B949EU);
  ui::drawLabel(drawList, canvas,
      designBox(x0 + pad, y0 + canvas.units(70.0F), panelWidth - pad * 2.0F,
          canvas.units(20.0F)),
      status, detail, ImGui::GetTime());

  const float cancelX = x0 + pad;
  const float cancelY = y0 + panelHeight - canvas.units(34.0F);
  const float cancelWidth = canvas.units(90.0F);
  const float cancelHeight = canvas.units(24.0F);
  fillPixels(cancelX, cancelY, cancelWidth, cancelHeight, 0xFF30363DU);
  ui::TextStyle cancelText;
  cancelText.sizeDp = 12.5F;
  cancelText.align = ui::HAlign::Center;
  ui::drawLabel(drawList, canvas,
      designBox(cancelX, cancelY, cancelWidth, cancelHeight), "取消", cancelText,
      ImGui::GetTime());
  const bool cancelPressed =
      ui::panelInteraction("##render_cancel", canvas,
          designBox(cancelX, cancelY, cancelWidth, cancelHeight),
          ui::rectPanel(0x00000000U, 0x00000000U))
          .pressed;
  ui::endFullscreenWindow();
  if (cancelPressed) {
    recording.cancel(context);
    notify(context, "已取消渲染");
  }
}

} // namespace rechigros::app
