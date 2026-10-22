#include "rechigros/platform/FileDialog.hpp"

#if defined(_WIN32)

// The Windows shell pickers live behind the common dialogs API, which needs COM.
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace rechigros::platform {
namespace {

// A one-shot COM apartment for the lifetime of a dialog. The renderer runs on the main
// thread, so the apartment is initialised and torn down around each call rather than once
// at start-up.
class ComScope final {
public:
  ComScope() {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    initialised_ = SUCCEEDED(result);
  }
  ~ComScope() {
    if (initialised_) CoUninitialize();
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

private:
  bool initialised_{};
};

// Runs the shared open dialog with `filter`, which is a pair of double-null terminated
// strings. Returns an empty path when the user cancels.
std::filesystem::path runOpenDialog(const wchar_t* filter, const wchar_t* title) {
  ComScope com;
  std::vector<wchar_t> buffer(32768, L'\0');
  OPENFILENAMEW options{};
  options.lStructSize = sizeof(options);
  options.hwndOwner = GetActiveWindow();
  options.lpstrFilter = filter;
  options.lpstrFile = buffer.data();
  options.nMaxFile = static_cast<DWORD>(buffer.size());
  options.lpstrTitle = title;
  options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&options) == FALSE) return {};
  return std::filesystem::path(buffer.data());
}

std::filesystem::path runFolderDialog(const wchar_t* title) {
  ComScope com;
  BROWSEINFOW info{};
  info.hwndOwner = GetActiveWindow();
  info.pszDisplayName = nullptr;
  info.lpszTitle = title;
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
  if (item == nullptr) return {};
  wchar_t path[MAX_PATH]{};
  const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
  CoTaskMemFree(item);
  if (!ok) return {};
  return std::filesystem::path(path);
}

} // namespace

std::filesystem::path FileDialog::openChartFile() {
  // The filter pairs are "description\0pattern\0", and the list ends with a double null.
  static const wchar_t kFilter[] =
      L"谱面文件 (*.json;*.txt)\0*.json;*.txt\0所有文件\0*.*\0\0";
  return runOpenDialog(kFilter, L"选择谱面文件");
}

std::filesystem::path FileDialog::openChartFolder() {
  return runFolderDialog(L"选择谱面文件夹");
}

std::filesystem::path FileDialog::openChartPackage() {
  static const wchar_t kFilter[] = L"谱面包 (*.zip)\0*.zip\0所有文件\0*.*\0\0";
  return runOpenDialog(kFilter, L"选择谱面包");
}

bool FileDialog::available() noexcept { return true; }

} // namespace rechigros::platform

#elif defined(__ANDROID__)

// The packaged build asks its own activity for the platform document picker. The answer
// travels back through the drop-file path, so these calls only raise the request and report
// that a dialog is on its way rather than waiting for it.
#include <SDL_system.h>
#include <jni.h>

namespace rechigros::platform {
namespace {

void requestPicker(int mode) {
  JNIEnv* environment = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
  if (environment == nullptr) return;
  jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
  if (activity == nullptr) return;
  jclass activityClass = environment->GetObjectClass(activity);
  if (activityClass == nullptr) {
    environment->DeleteLocalRef(activity);
    return;
  }
  const jmethodID method = environment->GetStaticMethodID(activityClass, "requestPicker", "(I)V");
  if (method != nullptr) environment->CallStaticVoidMethod(activityClass, method, mode);
  if (environment->ExceptionCheck()) environment->ExceptionClear();
  environment->DeleteLocalRef(activityClass);
  environment->DeleteLocalRef(activity);
}

constexpr int PICKER_FILE = 0;
constexpr int PICKER_FOLDER = 1;

} // namespace

std::filesystem::path FileDialog::openChartFile() {
  requestPicker(PICKER_FILE);
  return {};
}

std::filesystem::path FileDialog::openChartFolder() {
  requestPicker(PICKER_FOLDER);
  return {};
}

std::filesystem::path FileDialog::openChartPackage() {
  requestPicker(PICKER_FILE);
  return {};
}

// The picker is asynchronous here, so the answer arrives as a drop event instead of a
// return value. Callers should treat an empty result as "ask again later".
bool FileDialog::available() noexcept { return true; }

} // namespace rechigros::platform
#elif defined(__APPLE__)

// The document picker needs a presenting view controller, which this layer does not own,
// so callers fall back to a typed path on this platform.
namespace rechigros::platform {

std::filesystem::path FileDialog::openChartFile() { return {}; }
std::filesystem::path FileDialog::openChartFolder() { return {}; }
std::filesystem::path FileDialog::openChartPackage() { return {}; }
bool FileDialog::available() noexcept { return false; }

} // namespace rechigros::platform

#else

// No picker is implemented for this target, so callers fall back to a typed path.
namespace rechigros::platform {

std::filesystem::path FileDialog::openChartFile() { return {}; }
std::filesystem::path FileDialog::openChartFolder() { return {}; }
std::filesystem::path FileDialog::openChartPackage() { return {}; }
bool FileDialog::available() noexcept { return false; }

} // namespace rechigros::platform

#endif
