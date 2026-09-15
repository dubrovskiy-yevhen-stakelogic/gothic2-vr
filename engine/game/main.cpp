#include <Tempest/Window>
#include <Tempest/Application>
#include <Tempest/Log>

#include <zenkit/Logger.hh>

#include <Tempest/VulkanApi>

#if defined(_MSC_VER) && defined(TEMPEST_BUILD_DIRECTX12)
#include <Tempest/DirectX12Api>
#endif

#if defined(__APPLE__)
#include <Tempest/MetalApi>
#endif

#if defined(__IOS__) || defined(__ANDROID__)
#include "utils/installdetect.h"
#include <filesystem>
#endif

#include "utils/crashlog.h"
#include "mainwindow.h"
#include "gothic.h"
#include "build.h"
#include "commandline.h"
#include "diagnostics/baselinesmoke.h"
#include "vr/questxr.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#if defined(_MSC_VER)
#include <share.h>
#endif

#include <dmusic.h>

std::string_view selectDevice(const Tempest::AbstractGraphicsApi& api) {
  auto d = api.devices();

  static Tempest::Device::Props p;
  for(auto& i:d)
    // if(i.type==Tempest::DeviceType::Integrated) {
    if(i.type==Tempest::DeviceType::Discrete) {
      p = i;
      return p.name;
      }
  if(d.size()>0) {
    p = d[0];
    return p.name;
    }
  return "";
  }

std::unique_ptr<Tempest::AbstractGraphicsApi> mkApi(const CommandLine& g) {
  Tempest::ApiFlags flg = g.isValidationMode() ? Tempest::ApiFlags::Validation : Tempest::ApiFlags::NoFlags;
  switch(g.graphicsApi()) {
    case CommandLine::DirectX12:
#if defined(_MSC_VER) && defined(TEMPEST_BUILD_DIRECTX12)
      return std::make_unique<Tempest::DirectX12Api>(flg);
#else
      break;
#endif
    case CommandLine::Vulkan:
#if !defined(__APPLE__)
      return std::make_unique<Tempest::VulkanApi>(flg);
#else
      break;
#endif
    }

#if defined(__APPLE__)
  return std::make_unique<Tempest::MetalApi>(flg);
#else
  return std::make_unique<Tempest::VulkanApi>(flg);
#endif
  }

int main(int argc,const char** argv) {
  try {
    BaselineSmoke::preflight(argc, argv);
    }
  catch(const std::exception& e) {
    std::cerr << "Baseline preflight: " << e.what() << std::endl;
    return 2;
    }
#if defined(__IOS__) || defined(__ANDROID__)
  {
    auto appdir = InstallDetect::applicationSupportDirectory();
    if(!appdir.empty())
      std::filesystem::current_path(appdir);
  }
#endif

  try {
    static const std::unique_ptr<std::FILE,decltype(&std::fclose)> logFile(
#if defined(_MSC_VER)
      _fsopen("log.txt","wb",_SH_DENYWR),
#else
      std::fopen("log.txt","wb"),
#endif
      &std::fclose);
    if(!logFile)
      throw std::runtime_error("unable to open log.txt");
    Tempest::Log::setOutputCallback([](Tempest::Log::Mode, const char* text) {
      // Tempest serializes callbacks with its recursive logger mutex. Allow
      // concurrent readers and flush every record for diagnosis during loading.
      auto* file = logFile.get();
      const auto length = std::strlen(text);
      const bool writeFailed = std::fwrite(text,1,length,file)!=length;
      const bool newlineFailed = std::fputc('\n',file)==EOF;
      const bool flushFailed = std::fflush(file)!=0;
      if(writeFailed || newlineFailed || flushFailed)
        std::fputs("unable to write log.txt\n",stderr);
      });
    }
  catch(...) {
    Tempest::Log::e("unable to setup logfile - fallback to console log");
    }
  CrashLog::setup();

  zenkit::Logger::set(zenkit::LogLevel::INFO, [] (zenkit::LogLevel lvl, const char* cat, const char* message) {
    (void)cat;
    switch (lvl) {
      case zenkit::LogLevel::ERROR:
        Tempest::Log::e("[zenkit] ", message);
        break;
      case zenkit::LogLevel::WARNING:
        Tempest::Log::e("[zenkit] ", message);
        break;
      case zenkit::LogLevel::INFO:
        Tempest::Log::i("[zenkit] ", message);
        break;
      case zenkit::LogLevel::DEBUG:
      case zenkit::LogLevel::TRACE:
        Tempest::Log::d("[zenkit] ", message); // unused
        break;
      }
    });
  Dm_setLogger(DmLogLevel_INFO, [](void* ctx, DmLogLevel lvl, char const* msg) {
    switch (lvl) {
      case DmLogLevel_FATAL:
      case DmLogLevel_ERROR:
      case DmLogLevel_WARN:
        Tempest::Log::e("[dmusic] ", msg);
        break;
      case DmLogLevel_INFO:
        Tempest::Log::i("[dmusic] ", msg);
        break;
      case DmLogLevel_DEBUG:
      case DmLogLevel_TRACE:
        Tempest::Log::d("[dmusic] ", msg);
        break;
      }
    }, nullptr);

  Tempest::Log::i(appBuild);
  Workers::setThreadName("Main thread");

  CommandLine          cmd{argc,argv};
#if defined(GOTHIC2VR_OPENXR)
  QuestXr              xr;
#endif
  auto                 api     = mkApi(cmd);
  const auto           gpuName = selectDevice(*api);
  CrashLog::setGpu(gpuName);

  Tempest::Device      device{*api,gpuName};
  CrashLog::setGpu(device.properties().name);
#if defined(GOTHIC2VR_OPENXR)
  xr.attach(device);
  // The session and its swapchains must die before Tempest destroys VkDevice,
  // including exceptions from UI/world construction or application execution.
  struct XrSessionLifetime { QuestXr& xr; ~XrSessionLifetime() { xr.detach(); } } xrLifetime{xr};
#endif

  Resources            resources{device};
  Gothic               gothic;
  GameMusic            music;
  gothic.setupGlobalScripts();

  MainWindow           wx(device);
  Tempest::Application app;
  const int result = app.exec();
  return result==0 ? wx.baselineExitCode() : result;
  }
