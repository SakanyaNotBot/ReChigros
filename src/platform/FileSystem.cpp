#include "rechigros/platform/FileSystem.hpp"

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__ANDROID__) && __has_include(<iconv.h>)
#include <iconv.h>
#define RECHIGROS_HAS_ICONV 1
#endif

namespace rechigros::platform {
namespace {

std::filesystem::path g_assetRoot;
std::filesystem::path g_assetOverride;
std::filesystem::path g_userDataRoot;
std::once_flag g_resolveOnce;
std::mutex g_rootMutex;

std::filesystem::path resolveAssetRoot() {
  if (!g_assetOverride.empty()) return g_assetOverride;
  if (const char* overridePath = std::getenv("RECHIGROS_ASSETS")) {
    if (*overridePath) return pathFromUtf8(overridePath);
  }
  if (const char* base = SDL_GetBasePath()) {
    const std::filesystem::path candidate = pathFromUtf8(base) / "assets";
    std::error_code error;
    if (std::filesystem::is_directory(candidate, error)) return candidate;
  }
  std::error_code error;
  const auto working = std::filesystem::current_path(error) / "assets";
  if (!error && std::filesystem::is_directory(working, error)) return working;
  return std::filesystem::path("assets");
}

std::filesystem::path resolveUserDataRoot() {
  if (char* preference = SDL_GetPrefPath("com.wuying", "ReChigros")) {
    const std::filesystem::path result = pathFromUtf8(preference);
    SDL_free(preference);
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return result;
  }
  std::error_code error;
  const auto fallback = std::filesystem::temp_directory_path(error) / "ReChigros";
  std::filesystem::create_directories(fallback, error);
  return fallback;
}

} // namespace

std::filesystem::path pathFromUtf8(std::string_view value) {
  return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

const std::filesystem::path& assetRoot() {
  std::call_once(g_resolveOnce, [] { g_assetRoot = resolveAssetRoot(); });
  return g_assetRoot;
}

void setAssetRootOverride(const std::filesystem::path& root) {
  std::lock_guard<std::mutex> guard(g_rootMutex);
  g_assetOverride = root;
  g_assetRoot = root;
}

std::filesystem::path userDataRoot() {
  std::lock_guard<std::mutex> guard(g_rootMutex);
  if (g_userDataRoot.empty()) g_userDataRoot = resolveUserDataRoot();
  return g_userDataRoot;
}

std::filesystem::path cacheRoot() {
  const auto root = userDataRoot() / "cache";
  std::error_code error;
  std::filesystem::create_directories(root, error);
  return root;
}

namespace {
// Asks the platform for one of its shared media folders. A packaged build reaches them through
// the platform's own storage service, because they are not paths the app may assume; every other
// build asks the desktop environment, which reports them directly.
std::filesystem::path sharedMediaDirectory(const char* category, bool displayOnly = false) {
#if defined(__ANDROID__)
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return {};
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (activity == nullptr) return {};
  jclass cls = env->GetObjectClass(activity);
  env->DeleteLocalRef(activity);
  if (cls == nullptr) return {};
  // Releases that predate scoped storage refuse a write to the shared folders until the grant is
  // held, so it is requested once here rather than failing the write much later. The answer
  // arrives asynchronously; until then the per-user folder is used.
  if (!displayOnly) {
    if (const jmethodID ensure =
            env->GetStaticMethodID(cls, "ensureSharedStoragePermission", "()Z");
        ensure != nullptr) {
      (void)env->CallStaticBooleanMethod(cls, ensure);
      if (env->ExceptionCheck()) env->ExceptionClear();
    } else {
      env->ExceptionClear();
    }
  }
  std::filesystem::path result;
  const jmethodID method = env->GetStaticMethodID(cls,
      displayOnly ? "sharedStorageDisplayDirectory" : "sharedStorageDirectory",
      "(Ljava/lang/String;)Ljava/lang/String;");
  if (method != nullptr) {
    jstring name = env->NewStringUTF(category);
    auto text = static_cast<jstring>(
        env->CallStaticObjectMethod(cls, method, name));
    env->DeleteLocalRef(name);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    } else if (text != nullptr) {
      const char* utf8 = env->GetStringUTFChars(text, nullptr);
      if (utf8 != nullptr) {
        result = pathFromUtf8(utf8);
        env->ReleaseStringUTFChars(text, utf8);
      }
      env->DeleteLocalRef(text);
    }
  } else {
    env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
  return result;
#else
  const SDL_Folder folder = std::string_view(category) == "Movies" ? SDL_FOLDER_VIDEOS
      : std::string_view(category) == "Download"                    ? SDL_FOLDER_DOWNLOADS
                                                                    : SDL_FOLDER_DOCUMENTS;
  if (const char* known = SDL_GetUserFolder(folder)) return pathFromUtf8(known);
  return {};
#endif
}
} // namespace

std::filesystem::path publicVideoDirectory() {
  // Each platform keeps user-visible media in a different place, so the location is asked for
  // rather than guessed. The application's own name is appended so its recordings sit together
  // instead of mixing with every other program's files. When the platform reports nothing usable
  // the per-user directory is used, which always exists and is always writable.
  auto directory = sharedMediaDirectory("Movies");
  if (!directory.empty()) directory /= "Chigros";
  std::error_code error;
  if (!directory.empty()) {
    std::filesystem::create_directories(directory, error);
    error.clear();
    if (std::filesystem::is_directory(directory, error)) return directory;
  }
  const auto fallback = userDataRoot() / "recordings";
  std::filesystem::create_directories(fallback, error);
  return fallback;
}

std::filesystem::path publicVideoDisplayDirectory() {
  auto directory = sharedMediaDirectory("Movies", true);
  if (!directory.empty()) return directory / "Chigros";
  return publicVideoDirectory();
}

std::filesystem::path recordingWorkspace() {
  // A recording is written into a real directory, and on a release that forbids writing the
  // shared library by path there is no such directory to write to. The private staging folder
  // stands in for it; publishMedia hands the finished file over to the platform afterwards.
  auto shared = publicVideoDirectory();
  if (directoryIsWritable(shared)) return shared;
  std::error_code error;
  const auto staging = userDataRoot() / "staging";
  std::filesystem::create_directories(staging, error);
  return staging;
}

bool directoryIsWritable(const std::filesystem::path& directory) {
  if (directory.empty()) return false;
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) return false;
  // A directory that exists is not proof it accepts a file: the shared library on a recent
  // release is present and readable but refuses every write. Probing with a real file is the
  // only check that answers the question the caller is actually asking.
  const auto probe = directory / ".rechigros-write-probe";
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.put(0);
    if (!out) return false;
  }
  std::filesystem::remove(probe, error);
  return true;
}

std::filesystem::path publishMedia(const std::filesystem::path& file, const char* category) {
  if (file.empty()) return file;
#if defined(__ANDROID__)
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return file;
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (activity == nullptr) return file;
  jclass cls = env->GetObjectClass(activity);
  env->DeleteLocalRef(activity);
  if (cls == nullptr) return file;
  std::filesystem::path published = file;
  const jmethodID method = env->GetStaticMethodID(cls, "publishMediaFile",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
  if (method != nullptr) {
    const std::string path = pathToUtf8(file);
    jstring source = env->NewStringUTF(path.c_str());
    jstring kind = env->NewStringUTF(category != nullptr ? category : "Movies");
    auto result = static_cast<jstring>(
        env->CallStaticObjectMethod(cls, method, source, kind));
    env->DeleteLocalRef(source);
    env->DeleteLocalRef(kind);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    } else if (result != nullptr) {
      const char* utf8 = env->GetStringUTFChars(result, nullptr);
      if (utf8 != nullptr) {
        published = pathFromUtf8(utf8);
        env->ReleaseStringUTFChars(result, utf8);
      }
      env->DeleteLocalRef(result);
    }
  } else {
    env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
  return published;
#else
  // The desktop writes the shared folder directly, so there is nothing to hand over.
  (void)category;
  return file;
#endif
}

std::filesystem::path publicDownloadDirectory() {
  auto directory = sharedMediaDirectory("Download");
  if (!directory.empty()) directory /= "Chigros";
  std::error_code error;
  if (!directory.empty()) {
    std::filesystem::create_directories(directory, error);
    error.clear();
    if (std::filesystem::is_directory(directory, error)) return directory;
  }
  const auto fallback = userDataRoot() / "exports";
  std::filesystem::create_directories(fallback, error);
  return fallback;
}

std::filesystem::path executablePath() {
  std::error_code error;
  const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error && !self.empty()) return self;
  const char* base = SDL_GetBasePath();
  if (!base) return {};
  return pathFromUtf8(base);
}

bool isValidUtf8(std::string_view bytes) {
  std::size_t index = 0;
  while (index < bytes.size()) {
    const auto lead = static_cast<unsigned char>(bytes[index]);
    std::size_t length = 0;
    std::uint32_t code = 0;
    if (lead < 0x80U) {
      ++index;
      continue;
    }
    if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
      code = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
      code = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
      code = lead & 0x07U;
    } else {
      return false;
    }
    if (index + length > bytes.size()) return false;
    for (std::size_t offset = 1; offset < length; ++offset) {
      const auto continuation = static_cast<unsigned char>(bytes[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) return false;
      code = (code << 6U) | (continuation & 0x3FU);
    }
    // Reject overlong forms, surrogate halves and out-of-range values.
    const std::uint32_t minimum = length == 2 ? 0x80U : (length == 3 ? 0x800U : 0x10000U);
    if (code < minimum || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) {
      return false;
    }
    index += length;
  }
  return true;
}

std::string decodeLegacyText(std::string_view bytes) {
  const std::string original(bytes);
  if (isValidUtf8(bytes)) return original;
#if defined(_WIN32)
  if (bytes.empty()) return original;
  const int wideLength = MultiByteToWideChar(936, 0, bytes.data(),
      static_cast<int>(bytes.size()), nullptr, 0);
  if (wideLength <= 0) return original;
  std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
  if (MultiByteToWideChar(936, 0, bytes.data(), static_cast<int>(bytes.size()),
          wide.data(), wideLength) != wideLength) {
    return original;
  }
  const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0,
      nullptr, nullptr);
  if (utf8Length <= 0) return original;
  std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, utf8.data(), utf8Length,
          nullptr, nullptr) != utf8Length) {
    return original;
  }
  return utf8;
#elif defined(__ANDROID__)
  // The platform's iconv implementation is not available on every supported API level, so
  // the original bytes are kept instead of risking a wrong decoding.
  return original;
#elif defined(RECHIGROS_HAS_ICONV)
  iconv_t converter = iconv_open("UTF-8", "GBK");
  if (converter == reinterpret_cast<iconv_t>(-1)) return original;
  std::string result;
  result.reserve(bytes.size() * 2);
  char* input = const_cast<char*>(bytes.data());
  std::size_t inputLeft = bytes.size();
  char buffer[4096];
  while (inputLeft > 0) {
    char* output = buffer;
    std::size_t outputLeft = sizeof(buffer);
    const std::size_t converted = iconv(converter, &input, &inputLeft, &output, &outputLeft);
    result.append(buffer, sizeof(buffer) - outputLeft);
    if (converted == static_cast<std::size_t>(-1)) {
      if (errno == E2BIG) continue;
      iconv_close(converter);
      return original;
    }
  }
  iconv_close(converter);
  return result.empty() ? original : result;
#else
  return original;
#endif
}

std::string pathToUtf8(const std::filesystem::path& path) {
  // On Windows the narrow conversion uses the ANSI code page, so a path holding characters
  // outside that code page comes back as different bytes than the ones SDL and child processes
  // parse. Going through the native form and re-encoding as UTF-8 gives every consumer the same
  // text.
  const auto utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  const std::string bytes{std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
  return decodeLegacyText(bytes);
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return {};
  const auto size = input.tellg();
  if (size <= 0) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  return input ? bytes : std::vector<std::uint8_t>{};
}

bool writeTextFile(const std::filesystem::path& path, std::string_view content) {
  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(output);
}

bool writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& content) {
  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(reinterpret_cast<const char*>(content.data()),
      static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(output);
}

bool fileExists(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

bool directoryExists(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error);
}

std::uint64_t fileSize(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  return error ? 0 : static_cast<std::uint64_t>(size);
}

std::vector<std::filesystem::path> listFiles(const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> files;
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) return files;
  for (std::filesystem::recursive_directory_iterator iterator(directory, error), end;
       iterator != end; iterator.increment(error)) {
    if (error) break;
    if (!iterator->is_regular_file(error)) continue;
    files.push_back(iterator->path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

bool removeRecursively(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
  return !std::filesystem::exists(path, error);
}

std::string sanitiseFileName(const std::string& name) {
  std::string safe;
  safe.reserve(name.size());
  for (const char character : name) {
    switch (character) {
      case '\\': case '/': case ':': case '*': case '?':
      case '"': case '<': case '>': case '|': case '\0':
        safe.push_back('_');
        break;
      default:
        safe.push_back(character);
        break;
    }
  }
  if (safe.size() > 160) safe = safe.substr(safe.size() - 160);
  if (safe.empty() || safe == "." || safe == "..") return "unnamed";
  return safe;
}

std::filesystem::path joinPortable(const std::filesystem::path& base, std::string_view relative) {
  std::filesystem::path result = base;
  std::string component;
  component.reserve(relative.size());
  for (const char character : relative) {
    if (character == '/' || character == '\\') {
      if (!component.empty()) result /= pathFromUtf8(component);
      component.clear();
      continue;
    }
    component.push_back(character);
  }
  if (!component.empty()) result /= pathFromUtf8(component);
  return result;
}

std::string deviceDescription() {
#if defined(__ANDROID__)
  // The device model and the operating-system release both live on the Java side.
  std::string model;
  std::string release;
  if (JNIEnv* environment = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv())) {
    if (jobject activity = static_cast<jobject>(SDL_GetAndroidActivity())) {
      if (jclass activityClass = environment->GetObjectClass(activity)) {
        const auto readStaticString = [&](const char* className, const char* field) {
          std::string value;
          if (jclass target = environment->FindClass(className)) {
            if (jfieldID id = environment->GetStaticFieldID(target, field, "Ljava/lang/String;")) {
              if (auto* text = static_cast<jstring>(
                      environment->GetStaticObjectField(target, id))) {
                if (const char* utf = environment->GetStringUTFChars(text, nullptr)) {
                  value = utf;
                  environment->ReleaseStringUTFChars(text, utf);
                }
                environment->DeleteLocalRef(text);
              }
            }
            environment->DeleteLocalRef(target);
          }
          if (environment->ExceptionCheck()) environment->ExceptionClear();
          return value;
        };
        model = readStaticString("android/os/Build", "MODEL");
        release = readStaticString("android/os/Build$VERSION", "RELEASE");
        environment->DeleteLocalRef(activityClass);
      }
      environment->DeleteLocalRef(activity);
    }
  }
  if (model.empty() && release.empty()) return "Android";
  std::string result = model.empty() ? "Android" : model;
  if (!release.empty()) {
    result += " - Android ";
    result += release;
  }
  return result;
#elif defined(_WIN32)
  // The product name is the friendly model name; the build number stands in for a release.
  std::string model;
  if (const char* product = SDL_GetHint("SDL_WINDOWS_DEVICE_MODEL"); product != nullptr) {
    model = product;
  }
  if (model.empty()) model = "Windows";
  std::string result = model;
  if (const char* version = SDL_GetHint("SDL_WINDOWS_DEVICE_RELEASE");
      version != nullptr && *version != '\0') {
    result += " - Windows ";
    result += version;
  }
  return result;
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  return "iPhone - iOS";
#else
  return "Mac";
#endif
#else
  const char* platformName = SDL_GetPlatform();
  return platformName != nullptr ? platformName : "Unknown";
#endif
}

} // namespace rechigros::platform
