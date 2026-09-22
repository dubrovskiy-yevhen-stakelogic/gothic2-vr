#pragma once
// Where the VR hand meshes and their albedo come from: the file half of the
// Vr::Platform split that vrplatform.h owns for the OpenXR session. It is a
// separate header because vrplatform.h pulls in <openxr/openxr.h> and Tempest's
// gapi/vulkaninterop.h for that session glue, and an asset reader needs
// neither.
//
// Android reads the APK's assets/ (android/assets/vrhands, packaged by
// android/CMakeLists.txt) through the activity's AAssetManager, unchanged.
// Windows reads vrhands/ next to the executable, which engine/CMakeLists.txt
// fills at POST_BUILD. Deliberately not the working directory: the game is
// started with -g <GothicIIDir> and nothing guarantees the process ever runs
// from the folder it was installed into.
#if defined(GOTHIC2VR_OPENXR)
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>

#if defined(__ANDROID__)
#include "system/api/androidapi.h"
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <memory>
#else
#include <fstream>
#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// As in vrplatform.h: windows.h leaks object-like macros whose names are also
// enumerators and constants in ZenKit's public headers, which the VR
// translation units reach through resources.h. No Windows header follows, and
// #undef of an undefined macro is a no-op.
#undef ERROR
#undef VOID
#undef CONST
#undef TRANSPARENT
#undef OPAQUE
#undef small
#endif
#endif

namespace Vr {
namespace Platform {

// Upper bound on a hand asset, shared by both readers so the Quest keeps the
// size check it always had. The largest of the three is BigHandsAlbedo.png at
// ~0.7 MB.
constexpr int64_t assetSizeLimit=16000000;

#if !defined(__ANDROID__)
// Directory of the running executable, empty when the OS will not say; the
// caller then falls back to a relative path. There is no engine-wide helper for
// this: log.txt (game/main.cpp) and VR.ini (mainwindow.cpp) are both opened
// relative to the working directory, and InstallDetect only ever locates the
// *game* install, never our own binary.
inline std::filesystem::path executableDirectory() {
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH,L'\0');
  for(;;) {
    const DWORD n=GetModuleFileNameW(nullptr,buffer.data(),DWORD(buffer.size()));
    if(n==0) return {};                                  // no path available
    if(size_t(n)<buffer.size()) {buffer.resize(n);break;} // n excludes the terminator only on success
    if(buffer.size()>=32768) return {};                   // past the extended-path limit
    buffer.resize(buffer.size()*2);
    }
  return std::filesystem::path(buffer).parent_path();
#else
  std::error_code error;
  const auto self=std::filesystem::read_symlink("/proc/self/exe",error);
  return error?std::filesystem::path():self.parent_path();
#endif
  }

inline std::filesystem::path assetFile(const char* name) {
  const auto dir=executableDirectory();
  return dir.empty()?std::filesystem::path(name):dir/name;
  }
#endif

// VR.ini, read at startup and rewritten whenever a setting changes.
//
// Android keeps the plain working-directory name it has always used: the Quest
// changes directory into its app-support folder before the engine starts, so an
// existing profile is exactly where this leaves it.
//
// Windows is launched with -g <GothicIIDir> and the working directory is
// whatever started it — a shortcut's "Start in", a launcher, a console — so the
// settings belong next to the executable instead. A VR.ini already sitting in
// the working directory is still honoured when there is none beside the exe, so
// a profile written by an earlier build is adopted rather than lost; saving then
// keeps writing to that same file. Resolved once, because the reader and the
// writer have to agree even if the working directory moves later.
inline const std::filesystem::path& settingsFile() {
  static const std::filesystem::path resolved=[]() -> std::filesystem::path {
#if defined(__ANDROID__)
    return std::filesystem::path("VR.ini");
#else
    const auto beside=assetFile("VR.ini");
    std::error_code error;
    if(beside.has_parent_path() && !std::filesystem::exists(beside,error) && std::filesystem::exists("VR.ini",error))
      return std::filesystem::path("VR.ini");
    return beside;
#endif
    }();
  return resolved;
  }

// The temporary the writer renames over settingsFile(), always its neighbour so
// the rename stays within one directory and therefore one volume.
inline std::filesystem::path settingsTempFile() {
  auto temp=settingsFile(); temp+=".tmp"; return temp;
  }

// Human-readable location an asset was looked for, so a failure can tell the
// player which file to put where. Never used to open anything.
inline std::string assetPath(const char* name) {
#if defined(__ANDROID__)
  return std::string("<apk>/assets/")+name;
#else
  auto file=assetFile(name);
  return file.make_preferred().string();
#endif
  }

// The raw bytes of one hand asset. Both platforms keep the same size and
// truncation validation; format validation is HandAsset::read (uxrh.h).
inline std::vector<char> asset(const char* name) {
#if defined(__ANDROID__)
  auto app=Tempest::AndroidApi::nativeApp();
  if(!app || !app->activity) throw std::runtime_error("Android assets unavailable");
  std::unique_ptr<AAsset,decltype(&AAsset_close)> file(AAssetManager_open(app->activity->assetManager,name,AASSET_MODE_BUFFER),AAsset_close);
  if(!file) throw std::runtime_error(std::string("Missing hand asset: ")+name);
  auto n=AAsset_getLength64(file.get());
  if(n<=0 || n>assetSizeLimit) throw std::runtime_error("Invalid hand asset size");
  std::vector<char> bytes(static_cast<size_t>(n));size_t read=0;
  while(read<bytes.size()) {int got=AAsset_read(file.get(),bytes.data()+read,bytes.size()-read);if(got<=0)throw std::runtime_error("Truncated hand asset");read+=size_t(got);}
  return bytes;
#else
  const auto path=assetFile(name);
  std::error_code error;
  const auto length=std::filesystem::file_size(path,error);
  std::ifstream file(path,std::ios::binary);
  if(error || !file) throw std::runtime_error(std::string("Missing hand asset: ")+name);
  const auto n=static_cast<int64_t>(length);
  if(n<=0 || n>assetSizeLimit) throw std::runtime_error("Invalid hand asset size");
  std::vector<char> bytes(static_cast<size_t>(n));
  if(!file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("Truncated hand asset");
  return bytes;
#endif
  }

}
}
#endif
