#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <memory>
#include "gameplayprobe.h"
class PlayerControl;

// Opt-in integration check in a disposable working directory. Normal sessions
// never create fixtures, save, load or exit through this class.
class BaselineSmoke final {
  public:
    static void preflight(int argc, const char** argv);
    BaselineSmoke();
    void poll();
    void afterFrame(bool uiReady, bool videoActive, uint64_t dt, uint64_t phraseMs, size_t choices,
                    PlayerControl& player, const std::function<void()>& screenshot);
    int exitCode() const { return !enabled || passed ? 0 : 2; }

  private:
    enum Stage { AwaitWorld, Saving, Loading, Done };
    void report(const char* status, const std::string& reason);
    void finish(bool success, const std::string& reason);
    std::string snapshot() const;
    std::unique_ptr<GameplayProbe> gameplay;

    bool enabled = false;
    bool passed = false;
    Stage stage = AwaitWorld;
    unsigned worldFrames = 0;
    unsigned renderedWorldFrames = 0;
    bool previewCaptured = false;
    bool lastUiReady = false;
    bool lastVideoActive = false;
    uint64_t logicMilliseconds = 0;
    uint64_t lastPhraseMs = 0;
    size_t lastChoices = 0;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point lastReport;
    std::string before, after;
};
