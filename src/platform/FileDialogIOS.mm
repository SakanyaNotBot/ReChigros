#include "rechigros/platform/FileDialog.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL3/SDL_log.h>

#include <string>
#include <vector>

@interface RechigrosPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^completion)(NSArray<NSURL*>* urls);
@end

@implementation RechigrosPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  if (self.completion != nil) self.completion(urls);
  self.completion = nil;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
  (void)controller;
  if (self.completion != nil) self.completion(@[]);
  self.completion = nil;
}
@end

namespace rechigros::platform {
namespace {

// The picker hands back a security-scoped URL that stops being readable once the scope
// closes, and the importer needs a stable path for as long as it takes to unpack the
// package. Every answer is therefore copied into the application's temporary directory.
std::filesystem::path copyIntoSandbox(NSURL* url) {
  if (url == nil || url.path.length == 0) return {};
  const BOOL scoped = [url startAccessingSecurityScopedResource];
  NSFileManager* manager = [NSFileManager defaultManager];
  NSString* root = [NSTemporaryDirectory() stringByAppendingPathComponent:@"ReChigrosImport"];
  [manager createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil];
  NSString* destination = [root stringByAppendingPathComponent:url.lastPathComponent];
  [manager removeItemAtPath:destination error:nil];
  NSError* error = nil;
  const BOOL copied = [manager copyItemAtPath:url.path toPath:destination error:&error];
  if (scoped) [url stopAccessingSecurityScopedResource];
  if (!copied) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[import] could not copy the picked item: %s",
        error != nil ? error.localizedDescription.UTF8String : "unknown error");
    return {};
  }
  return std::filesystem::path(destination.fileSystemRepresentation);
}

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
  while (root.presentedViewController != nil) root = root.presentedViewController;
  return root;
}

// The document types are UTI strings rather than file extensions: the picker wants types,
// and the packaged formats have no system type of their own, so they are declared as plain
// data (or the archive type where one exists).
NSArray<NSString*>* documentTypes(bool folder, const std::vector<const char*>& extensions) {
  if (folder) return @[ @"public.folder" ];
  NSMutableArray<NSString*>* types = [NSMutableArray array];
  for (const char* extension : extensions) {
    if (extension == nullptr) continue;
    NSString* value = [NSString stringWithUTF8String:extension];
    NSString* type = nil;
    if ([value isEqualToString:@"zip"] || [value isEqualToString:@"pez"]) {
      type = @"public.zip-archive";
    } else if ([value isEqualToString:@"json"]) {
      type = @"public.json";
    } else if ([value isEqualToString:@"txt"]) {
      type = @"public.plain-text";
    }
    // pez and cgrp share the archive container but have no registered type; keeping the
    // archive type visible also keeps the files selectable in the Files browser.
    if (type == nil) type = @"public.data";
    if (![types containsObject:type]) [types addObject:type];
  }
  if (types.count == 0) [types addObject:@"public.data"];
  return types;
}

// Presents the system picker and runs the main run loop until it answers. The interface is
// single-threaded: blocking with a plain sleep would freeze the picker it just presented,
// while a nested run loop keeps it interactive.
std::vector<std::filesystem::path> pickItems(bool folder, const std::vector<const char*>& extensions,
    bool multiple) {
  UIViewController* host = presentingController();
  if (host == nil) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[import] no view controller to present from");
    return {};
  }
  __block std::vector<std::filesystem::path> picked;
  __block bool finished = NO;
  RechigrosPickerDelegate* delegate = [[RechigrosPickerDelegate alloc] init];
  delegate.completion = ^(NSArray<NSURL*>* urls) {
    for (NSURL* url in urls) {
      auto path = copyIntoSandbox(url);
      if (!path.empty()) picked.push_back(std::move(path));
    }
    finished = YES;
  };
  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc] initWithDocumentTypes:documentTypes(folder, extensions)
                                                             inMode:UIDocumentPickerModeImport];
  picker.delegate = delegate;
  picker.allowsMultipleSelection = multiple;
  // On a phone the picker is presented full screen; the form sheet style is ignored there
  // and keeps the iPad presentation compact.
  picker.modalPresentationStyle = UIModalPresentationFormSheet;
  [host presentViewController:picker animated:YES completion:nil];
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:600.0];
  while (!finished && [deadline timeIntervalSinceNow] > 0.0) {
    @autoreleasepool {
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                               beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
  }
  if (!finished) {
    // The picker is still up after ten minutes; treat it as cancelled rather than leaving
    // the caller blocked forever.
    [host dismissViewControllerAnimated:NO completion:nil];
  }
  return picked;
}

std::filesystem::path pickOne(bool folder, const std::vector<const char*>& extensions) {
  auto items = pickItems(folder, extensions, false);
  return items.empty() ? std::filesystem::path{} : items.front();
}

} // namespace

std::filesystem::path FileDialog::openChartFile() {
  return pickOne(false, {"json", "pec", "txt"});
}

std::filesystem::path FileDialog::openChartFolder() {
  return pickOne(true, {});
}

std::filesystem::path FileDialog::openChartPackage() {
  return pickOne(false, {"zip", "pez", "cgrp"});
}

std::filesystem::path FileDialog::openReplayFile() {
  return pickOne(false, {"cgrp", "zip", "json"});
}

std::filesystem::path FileDialog::openSkinPackage() {
  return pickOne(false, {"zip", "pez"});
}

std::filesystem::path FileDialog::pickDirectory() {
  return pickOne(true, {});
}

std::vector<std::filesystem::path> FileDialog::openChartSourceFiles() {
  return pickItems(false, {"json", "pec", "txt", "ogg", "wav", "mp3", "png", "jpg", "jpeg"}, true);
}

bool FileDialog::available() noexcept { return true; }

} // namespace rechigros::platform

#endif // iOS
