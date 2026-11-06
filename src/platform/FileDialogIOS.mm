#include "rechigros/platform/FileDialog.hpp"

#include "rechigros/platform/FileSystem.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <dispatch/dispatch.h>
#include <SDL3/SDL_log.h>

#include <string>
#include <utility>
#include <vector>

using RechigrosPickerCompletion =
    void (^)(UIDocumentPickerViewController* controller, NSArray<NSURL*>* urls);

@interface RechigrosPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, copy) RechigrosPickerCompletion completion;
@end

static bool g_pickerActive = false;
static unsigned long g_pickerGeneration = 0;
static RechigrosPickerDelegate* g_pickerDelegate = nil;
// The chosen export directory is held open for the rest of the session: a security-scoped
// URL stops being writable once its scope closes, and the recording is written much later.
static NSURL* g_outputDirectoryURL = nil;

@implementation RechigrosPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  RechigrosPickerCompletion completion = self.completion;
  self.completion = nil;
  if (completion != nil) completion(controller, urls);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
  (void)controller;
  self.completion = nil;
  g_pickerActive = false;
  rechigros::platform::queuePickerResult("pickercancel", std::filesystem::path{});
}
@end

namespace rechigros::platform {
namespace {

namespace fs = std::filesystem;

UIViewController* presentingController() {
  UIViewController* root = nil;
  for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
    if (![scene isKindOfClass:[UIWindowScene class]]) continue;
    for (UIWindow* window in ((UIWindowScene*)scene).windows) {
      if (window.isKeyWindow) {
        root = window.rootViewController;
        break;
      }
    }
    if (root != nil) break;
  }
  if (root == nil) {
    // A scene that has not become key yet still owns the only window the application has.
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
      if (![scene isKindOfClass:[UIWindowScene class]]) continue;
      for (UIWindow* window in ((UIWindowScene*)scene).windows) {
        root = window.rootViewController;
        if (root != nil) break;
      }
      if (root != nil) break;
    }
  }
  if (root == nil) {
    // Some launch and resume paths have not published a scene window yet; the application's
    // key window is the same SDL window and remains the correct host for the picker.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    root = [UIApplication sharedApplication].keyWindow.rootViewController;
#pragma clang diagnostic pop
  }
  while (root.presentedViewController != nil) root = root.presentedViewController;
  return root;
}

// The document types are UTI strings rather than file extensions: the picker wants types.
// The package type is declared by the application itself, so pez and cgrp keep their
// extension-specific entry instead of widening the picker to every document.
NSArray<NSString*>* documentTypes(bool folder, const std::vector<const char*>& extensions) {
  if (folder) return @[ @"public.folder" ];
  NSMutableArray<NSString*>* types = [NSMutableArray array];
  for (const char* extension : extensions) {
    if (extension == nullptr) continue;
    NSString* value = [NSString stringWithUTF8String:extension];
    NSString* type = nil;
    if ([value isEqualToString:@"zip"]) {
      type = @"public.zip-archive";
    } else if ([value isEqualToString:@"pez"] || [value isEqualToString:@"cgrp"]) {
      type = @"com.wuying.chigros.package";
    } else if ([value isEqualToString:@"json"]) {
      type = @"public.json";
    } else if ([value isEqualToString:@"txt"]) {
      type = @"public.plain-text";
    } else if ([value isEqualToString:@"ogg"] || [value isEqualToString:@"mp3"]
        || [value isEqualToString:@"wav"] || [value isEqualToString:@"m4a"]
        || [value isEqualToString:@"aac"] || [value isEqualToString:@"flac"]) {
      type = @"public.audio";
    } else if ([value isEqualToString:@"png"] || [value isEqualToString:@"jpg"]
        || [value isEqualToString:@"jpeg"] || [value isEqualToString:@"webp"]) {
      type = @"public.image";
    }
    if (type == nil) type = @"public.data";
    if (![types containsObject:type]) [types addObject:type];
  }
  if (types.count == 0) [types addObject:@"public.data"];
  return types;
}

std::string errorText(NSError* error, const char* fallback) {
  if (error != nil && error.localizedDescription != nil) {
    return error.localizedDescription.UTF8String;
  }
  return fallback;
}

NSString* nativePath(const fs::path& path) {
  return [NSString stringWithUTF8String:pathToUtf8(path).c_str()];
}

fs::path incomingRoot() {
  std::error_code error;
  const fs::path root = cacheRoot() / "incoming";
  fs::create_directories(root, error);
  return error ? fs::path{} : root;
}

fs::path newRequestDirectory() {
  const fs::path root = incomingRoot();
  if (root.empty()) return {};
  const fs::path directory =
      root / pathFromUtf8([[[NSUUID UUID] UUIDString] UTF8String]);
  std::error_code error;
  fs::create_directories(directory, error);
  return error ? fs::path{} : directory;
}

struct CopyOutcome {
  fs::path path;
  std::string error;
};

// Imported documents are owned by the application after the picker's delegate returns, so
// the selected item is moved into the cache when possible. Directories and in-place URLs
// are copied because moving them would alter the user's own document.
CopyOutcome adoptIntoCache(NSURL* url, const fs::path& target, bool allowMove) {
  if (url == nil || url.path.length == 0) {
    return {{}, "无法读取所选文件：没有本地路径"};
  }
  std::error_code directoryError;
  fs::create_directories(target.parent_path(), directoryError);
  if (directoryError) return {{}, "无法创建导入缓存目录"};

  const BOOL scoped = [url startAccessingSecurityScopedResource];
  NSFileManager* manager = [NSFileManager defaultManager];
  NSString* destination = nativePath(target);
  NSError* error = nil;
  BOOL copied = NO;
  if (allowMove) {
    copied = [manager moveItemAtPath:url.path toPath:destination error:&error];
    if (!copied) error = nil;
  }
  if (!copied) {
    copied = [manager copyItemAtPath:url.path toPath:destination error:&error];
  }
  if (scoped) [url stopAccessingSecurityScopedResource];
  if (!copied) {
    std::error_code ignored;
    fs::remove_all(target, ignored);
    // The request directory is exclusive to this answer, so an empty one can go too.
    fs::remove(target.parent_path(), ignored);
    const std::string detail = errorText(error, "unknown error");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[import] could not adopt the picked item: %s",
        detail.c_str());
    return {{}, "无法读取所选文件：" + detail};
  }
  return {target, {}};
}

// Queues the answer only after the picker has finished dismissing. Starting the next picker
// of a multi-step import while the previous one is still on screen makes UIKit refuse the
// presentation and leaves the flow stuck.
void finishPicker(UIDocumentPickerViewController* controller, std::string tag,
    fs::path path, std::string error) {
  const std::string resultTag = std::move(tag);
  const fs::path resultPath = std::move(path);
  const std::string resultError = std::move(error);
  [controller dismissViewControllerAnimated:YES completion:^{
    if (!resultError.empty()) {
      queuePickerResult("pickererror", pathFromUtf8(resultError));
      return;
    }
    if (!resultTag.empty()) queuePickerResult(resultTag, resultPath);
  }];
}

NSString* trimmedLeafName(NSURL* url, const char* fallback) {
  NSString* name = url.lastPathComponent;
  if (name == nil || name.length == 0) name = [NSString stringWithUTF8String:fallback];
  return [NSString stringWithUTF8String:sanitiseFileName(name.UTF8String).c_str()];
}

void presentPicker(bool folder, bool directoryOnly, const std::vector<const char*>& extensions,
    const char* tag) {
  if (g_pickerActive) {
    queuePickerResult("pickererror", pathFromUtf8("已有文件选择器正在打开"));
    return;
  }
  UIViewController* host = presentingController();
  if (host == nil) {
    queuePickerResult("pickererror", pathFromUtf8("无法显示文件选择器"));
    return;
  }

  const std::string requestedTag = tag != nullptr ? tag : "";
  RechigrosPickerDelegate* delegate = [[RechigrosPickerDelegate alloc] init];
  g_pickerDelegate = delegate;
  g_pickerActive = true;
  const unsigned long generation = ++g_pickerGeneration;
  delegate.completion = ^(UIDocumentPickerViewController* controller, NSArray<NSURL*>* urls) {
    g_pickerActive = false;
    if (urls.count == 0) {
      finishPicker(controller, {}, {}, {});
      queuePickerResult("pickercancel", fs::path{});
      return;
    }
    NSURL* url = urls.firstObject;
    if (url == nil) {
      finishPicker(controller, {}, {}, "无法读取所选文件");
      return;
    }

    if (directoryOnly) {
      const BOOL scoped = [url startAccessingSecurityScopedResource];
      if (g_outputDirectoryURL != nil) {
        [g_outputDirectoryURL stopAccessingSecurityScopedResource];
      }
      g_outputDirectoryURL = scoped ? url : nil;
      const fs::path path = pathFromUtf8(url.path.UTF8String);
      finishPicker(controller, requestedTag, path, {});
      return;
    }

    const fs::path request = newRequestDirectory();
    if (request.empty()) {
      finishPicker(controller, {}, {}, "无法创建导入缓存目录");
      return;
    }
    const fs::path target = request / pathFromUtf8(trimmedLeafName(url, "import").UTF8String);
    const bool allowMove = !folder;
    CopyOutcome copied = adoptIntoCache(url, target, allowMove);
    finishPicker(controller, requestedTag, std::move(copied.path), std::move(copied.error));
  };

  UIDocumentPickerMode mode = (folder || directoryOnly) ? UIDocumentPickerModeOpen
                                                         : UIDocumentPickerModeImport;
  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc] initWithDocumentTypes:documentTypes(folder, extensions)
                                                             inMode:mode];
  picker.delegate = delegate;
  picker.allowsMultipleSelection = false;
  // On a phone the picker is presented full screen; the form sheet style is ignored there
  // and keeps the iPad presentation compact.
  picker.modalPresentationStyle = UIModalPresentationFormSheet;
  [host presentViewController:picker animated:YES completion:^{
    if (picker.presentingViewController == nil) {
      g_pickerActive = false;
      queuePickerResult("pickererror", pathFromUtf8("无法显示文件选择器"));
    }
  }];
  // Presentation has no error callback on the older document-picker API. If UIKit refuses
  // the request silently, the flag must not stay latched and block every later picker.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2 * NSEC_PER_SEC)),
      dispatch_get_main_queue(), ^{
        if (generation != g_pickerGeneration || !g_pickerActive) return;
        if (picker.presentingViewController != nil) return;
        g_pickerActive = false;
        queuePickerResult("pickererror", pathFromUtf8("无法显示文件选择器"));
      });
}

} // namespace

std::filesystem::path FileDialog::openChartFile() {
  presentPicker(false, false, {"json", "pec", "rpe"}, "chartfile");
  return {};
}

std::filesystem::path FileDialog::openChartFolder() {
  presentPicker(true, false, {}, "chartdir");
  return {};
}

std::filesystem::path FileDialog::openChartPackage() {
  presentPicker(false, false, {"zip", "pez", "cgrp"}, "chartzip");
  return {};
}

std::filesystem::path FileDialog::openAudioFile() {
  presentPicker(false, false, {"ogg", "mp3", "wav", "m4a", "aac", "flac"}, "audiofile");
  return {};
}

std::filesystem::path FileDialog::openImageFile() {
  presentPicker(false, false, {"png", "jpg", "jpeg", "webp"}, "imagefile");
  return {};
}

std::filesystem::path FileDialog::openReplayFile() {
  presentPicker(false, false, {"cgrp", "zip", "json"}, "replay");
  return {};
}

std::filesystem::path FileDialog::openSkinPackage() {
  presentPicker(false, false, {"zip", "pez"}, "skin");
  return {};
}

std::filesystem::path FileDialog::pickDirectory() {
  presentPicker(true, true, {}, "pickdir");
  return {};
}

bool FileDialog::asynchronous() noexcept { return true; }

bool FileDialog::supportsChartFolder() noexcept { return true; }

bool FileDialog::supportsDirectoryPicker() noexcept { return true; }

bool FileDialog::available() noexcept { return true; }

} // namespace rechigros::platform

#endif // iOS
