#include "vr/vrcontrols.h"
#include "vr/vrdefaults.h"
#include <sstream>
#include <cstdlib>
#include <cstdio>
int main() {
  Vr::Settings fresh;
  std::istringstream input(Vr::ReleaseDefaults);
  fresh.read(input);
  if(fresh.welcomeSeen || fresh.profiler || !fresh.interaction.pickupHighlight ||
     !fresh.interaction.ignoreWeaponRequirements || std::abs(fresh.hudX-.225f)>.0001f ||
     std::abs(fresh.hudDistance-2.5f)>.0001f) return EXIT_FAILURE;
  std::ostringstream saved;
  fresh.write(saved);
  if(saved.str().find("ItemCal_DEFAULT_BOW_R=-0.015 -0.005 0.04 0 -97.5 0") == std::string::npos ||
     saved.str().find("ItemCal_DEFAULT_CROSSBOW_R=-0.04 0.02 0.02 0 180 47.5") == std::string::npos)
    return EXIT_FAILURE;
  fresh.welcomeSeen=true;
  std::ostringstream afterWelcome;fresh.write(afterWelcome);
  Vr::Settings nextSession;std::istringstream reload(afterWelcome.str());nextSession.read(reload);
  if(!nextSession.welcomeSeen || nextSession.hudX!=fresh.hudX) return EXIT_FAILURE;
  std::puts("Release defaults: 10 settings/calibration/persistence checks passed");
}
