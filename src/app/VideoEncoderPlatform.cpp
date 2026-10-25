#include "rechigros/app/VideoEncoderPlatform.hpp"

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <cstdint>
#include <string>

namespace rechigros::app::platformenc {
namespace {

// Every call resolves the activity's static bridge methods on demand. The activity is destroyed
// and recreated across configuration changes, so a cached class reference would be dangling after
// a restart; resolving costs two lookups per recording, not per frame.
struct Bridge {
  JNIEnv* env{};
  jclass cls{};
  bool valid{};

  Bridge() {
    env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) return;
    auto* activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (activity == nullptr) return;
    cls = static_cast<jclass>(env->GetObjectClass(activity));
    env->DeleteLocalRef(activity);
    valid = cls != nullptr;
  }
  ~Bridge() {
    if (env != nullptr && cls != nullptr) env->DeleteLocalRef(cls);
  }
  Bridge(const Bridge&) = delete;
  Bridge& operator=(const Bridge&) = delete;
};

std::string takeException(JNIEnv* env, const char* fallback) {
  if (env == nullptr || !env->ExceptionCheck()) return fallback;
  env->ExceptionClear();
  return fallback;
}

} // namespace

bool available() noexcept { return SDL_GetAndroidJNIEnv() != nullptr; }

bool open(int width, int height, int fps, int quality, const std::string& outputPath,
    std::string& error) {
  Bridge bridge;
  if (!bridge.valid) {
    error = "the platform encoder bridge is unavailable";
    return false;
  }
  const jmethodID method = bridge.env->GetStaticMethodID(bridge.cls, "startVideoEncoder",
      "(Ljava/lang/String;IIII)Z");
  if (method == nullptr) {
    error = takeException(bridge.env, "the platform encoder entry point is missing");
    bridge.env->ExceptionClear();
    error = "the platform encoder entry point is missing";
    return false;
  }
  jstring path = bridge.env->NewStringUTF(outputPath.c_str());
  const jboolean started = bridge.env->CallStaticBooleanMethod(bridge.cls, method, path,
      static_cast<jint>(width), static_cast<jint>(height), static_cast<jint>(fps),
      static_cast<jint>(quality));
  bridge.env->DeleteLocalRef(path);
  if (bridge.env->ExceptionCheck()) {
    bridge.env->ExceptionClear();
    error = "the platform encoder refused to start";
    return false;
  }
  if (started == JNI_FALSE) {
    error = "the platform encoder could not be created";
    return false;
  }
  return true;
}

bool writeFrame(int width, int height, const std::uint8_t* pixels, std::size_t bytes,
    std::string& error) {
  if (width <= 0 || height <= 0 || pixels == nullptr || bytes < 4U) {
    error = "the frame is empty";
    return false;
  }
  Bridge bridge;
  if (!bridge.valid) {
    error = "the platform encoder bridge is unavailable";
    return false;
  }
  const jmethodID method = bridge.env->GetStaticMethodID(bridge.cls, "queueVideoFrame", "([BII)Z");
  if (method == nullptr) {
    bridge.env->ExceptionClear();
    error = "the platform frame entry point is missing";
    return false;
  }
  jbyteArray array = bridge.env->NewByteArray(static_cast<jsize>(bytes));
  if (array == nullptr) {
    bridge.env->ExceptionClear();
    error = "the frame could not be handed to the platform encoder";
    return false;
  }
  bridge.env->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes),
      reinterpret_cast<const jbyte*>(pixels));
  const jboolean queued = bridge.env->CallStaticBooleanMethod(bridge.cls, method, array,
      static_cast<jint>(width), static_cast<jint>(height));
  bridge.env->DeleteLocalRef(array);
  if (bridge.env->ExceptionCheck()) {
    bridge.env->ExceptionClear();
    error = "the platform encoder rejected a frame";
    return false;
  }
  if (queued == JNI_FALSE) {
    error = "the platform encoder stopped accepting frames";
    return false;
  }
  return true;
}

bool finish(std::string& error) {
  Bridge bridge;
  if (!bridge.valid) {
    error = "the platform encoder bridge is unavailable";
    return false;
  }
  const jmethodID method = bridge.env->GetStaticMethodID(bridge.cls, "finishVideoEncoder", "()Z");
  if (method == nullptr) {
    bridge.env->ExceptionClear();
    error = "the platform finish entry point is missing";
    return false;
  }
  const jboolean done = bridge.env->CallStaticBooleanMethod(bridge.cls, method);
  if (bridge.env->ExceptionCheck()) {
    bridge.env->ExceptionClear();
    error = "the platform encoder did not finish";
    return false;
  }
  if (done == JNI_FALSE) {
    error = "the platform encoder did not finish";
    return false;
  }
  return true;
}

void abort() noexcept {
  Bridge bridge;
  if (!bridge.valid) return;
  const jmethodID method = bridge.env->GetStaticMethodID(bridge.cls, "abortVideoEncoder", "()V");
  if (method == nullptr) {
    bridge.env->ExceptionClear();
    return;
  }
  bridge.env->CallStaticVoidMethod(bridge.cls, method);
  if (bridge.env->ExceptionCheck()) bridge.env->ExceptionClear();
}

// Hands the raw audio track to the platform muxer. The samples are interleaved stereo floats, the
// same layout the desktop encoder receives, so the caller does not change per platform.
bool addAudioTrack(const std::string& pcmPath, int sampleRate, int delayMs, std::string& error) {
  Bridge bridge;
  if (!bridge.valid) {
    error = "the platform encoder bridge is unavailable";
    return false;
  }
  const jmethodID method = bridge.env->GetStaticMethodID(bridge.cls, "setVideoAudioTrack",
      "(Ljava/lang/String;III)Z");
  if (method == nullptr) {
    bridge.env->ExceptionClear();
    error = "the platform audio entry point is missing";
    return false;
  }
  jstring path = bridge.env->NewStringUTF(pcmPath.c_str());
  const jboolean ok = bridge.env->CallStaticBooleanMethod(bridge.cls, method, path,
      static_cast<jint>(sampleRate), static_cast<jint>(2), static_cast<jint>(delayMs));
  bridge.env->DeleteLocalRef(path);
  if (bridge.env->ExceptionCheck()) {
    bridge.env->ExceptionClear();
    error = "the platform encoder rejected the audio track";
    return false;
  }
  if (ok == JNI_FALSE) {
    error = "the platform encoder could not take the audio track";
    return false;
  }
  return true;
}

} // namespace rechigros::app::platformenc

#else

// Desktop and console builds encode through the external tool, so this bridge has nothing to do.
// Defining the same symbols keeps one call site in VideoEncoder.cpp for every platform.

namespace rechigros::app::platformenc {

bool available() noexcept { return false; }
bool open(int, int, int, int, const std::string&, std::string&) { return false; }
bool writeFrame(int, int, const std::uint8_t*, std::size_t, std::string&) { return false; }
bool finish(std::string&) { return false; }
void abort() noexcept {}
bool addAudioTrack(const std::string&, int, int, std::string&) { return false; }

} // namespace rechigros::app::platformenc

#endif
