#include "feedback.h"

#include "gothic.h"
#include "hapticpolicy.h"

#include <Tempest/Application>
#include <Tempest/SystemApi>
#include <mutex>

namespace {
std::mutex feedbackMutex;
HapticPolicy policy;
}

void Feedback::setEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(feedbackMutex);
  if(policy.setEnabled(enabled))
    Tempest::SystemApi::vibrate(0,0.f,false);
  }

void Feedback::setGamepad(bool gamepad) {
  std::lock_guard<std::mutex> lock(feedbackMutex);
  if(policy.setGamepad(gamepad))
    Tempest::SystemApi::vibrate(0,0.f,false);
  }

void Feedback::play(Effect effect) {
#if defined(__ANDROID__)
  std::lock_guard<std::mutex> lock(feedbackMutex);
  const auto pulse=policy.next(effect,Tempest::Application::tickCount());
  if(pulse.duration!=0)
    Tempest::SystemApi::vibrate(pulse.duration,pulse.strength,pulse.gamepad);
#else
  (void)effect;
#endif
  }

void Feedback::gameplay(Effect effect) {
#if defined(__ANDROID__)
  auto& gothic=Gothic::inst();
  if(gothic.checkLoading()==Gothic::LoadState::Idle && !gothic.isPause() && gothic.isInGame())
    play(effect);
#else
  (void)effect;
#endif
  }

void Feedback::spellCharge(float intensity) {
#if defined(__ANDROID__)
  auto& gothic=Gothic::inst();
  if(gothic.checkLoading()!=Gothic::LoadState::Idle || gothic.isPause() || !gothic.isInGame())
    return;
  std::lock_guard<std::mutex> lock(feedbackMutex);
  const auto pulse=policy.charge(intensity,Tempest::Application::tickCount());
  if(pulse.duration!=0)
    Tempest::SystemApi::vibrate(pulse.duration,pulse.strength,pulse.gamepad);
#else
  (void)intensity;
#endif
  }
