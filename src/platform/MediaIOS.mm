// iOS media publishing: a finished recording is copied into the system photo library so it
// appears in the Photos app, the way the Android bridge hands a video to the media store. The
// sandbox copy is kept, so a denied permission still leaves the file in its Files-visible folder.

#include "rechigros/platform/FileSystem.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE

#import <Foundation/Foundation.h>
#import <Photos/Photos.h>

#include <SDL3/SDL_log.h>

namespace rechigros::platform {

bool saveVideoToPhotos(const std::filesystem::path& file) {
  if (file.empty()) return false;
  const std::string utf8 = pathToUtf8(file);
  NSString* path = [NSString stringWithUTF8String:utf8.c_str()];
  if (path == nil) return false;
  NSURL* url = [NSURL fileURLWithPath:path];
  if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) return false;

  // Add-only access is enough to save a video and is the least privilege the publish needs.
  // The answer arrives on a Photos queue; the bounded wait keeps a never-answered prompt from
  // parking the interface for ever.
  __block PHAuthorizationStatus status = PHAuthorizationStatusNotDetermined;
  dispatch_semaphore_t answered = dispatch_semaphore_create(0);
  if (@available(iOS 14.0, *)) {
    [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly
        handler:^(PHAuthorizationStatus result) {
          status = result;
          dispatch_semaphore_signal(answered);
        }];
  } else {
    [PHPhotoLibrary requestAuthorization:^(PHAuthorizationStatus result) {
      status = result;
      dispatch_semaphore_signal(answered);
    }];
  }
  dispatch_semaphore_wait(answered, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
  bool granted = status == PHAuthorizationStatusAuthorized;
  if (@available(iOS 14.0, *)) {
    granted = granted || status == PHAuthorizationStatusLimited;
  }
  if (!granted) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "[record] photo library access not granted; the video stays in its folder");
    return false;
  }

  NSError* error = nil;
  const BOOL saved = [[PHPhotoLibrary sharedPhotoLibrary] performChangesAndWait:^{
    [PHAssetChangeRequest creationRequestForAssetFromVideoAtFileURL:url];
  } error:&error];
  if (!saved) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[record] photo library save failed: %s",
        error.localizedDescription.UTF8String ?: "unknown");
    return false;
  }
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[record] saved to the photo library");
  return true;
}

} // namespace rechigros::platform

#endif // __APPLE__ && TARGET_OS_IPHONE
