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
     fresh.interaction.ignoreWeaponRequirements || std::abs(fresh.hudX-.225f)>.0001f ||
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

  // The release defaults are Touch-shaped, and a controller without face buttons
  // or a stick click has to fall back without disturbing anything a player edited.
  if(fresh.mapping!=Vr::Settings::touchMapping) return EXIT_FAILURE;
  Vr::Settings wand;std::istringstream wandInput(Vr::ReleaseDefaults);wand.read(wandInput);
  wand.adoptControllerDefaults(true);
  if(wand.mapping!=Vr::Settings::wandMapping) return EXIT_FAILURE;
  wand.adoptControllerDefaults(false);
  if(wand.mapping!=Vr::Settings::touchMapping) return EXIT_FAILURE;
  wand.mapping[1]=6;const auto edited=wand.mapping;
  wand.adoptControllerDefaults(true);wand.adoptControllerDefaults(false);
  if(wand.mapping!=edited) return EXIT_FAILURE;
  // A profile that already reads as the wand map was chosen, not adopted: leave it.
  Vr::Settings chosen;chosen.mapping=Vr::Settings::wandMapping;
  chosen.adoptControllerDefaults(false);
  if(chosen.mapping!=Vr::Settings::wandMapping) return EXIT_FAILURE;
  // An untouched fallback is the controller's, not the profile's, and must not
  // overwrite the button layout the next Touch session reads back.
  Vr::Settings onWand;std::istringstream onWandInput(Vr::ReleaseDefaults);onWand.read(onWandInput);
  onWand.adoptControllerDefaults(true);
  std::ostringstream wandSaved;onWand.write(wandSaved);
  if(wandSaved.str().find("ButtonMap1=2")==std::string::npos) return EXIT_FAILURE;
  onWand.mapping[0]=5;
  std::ostringstream wandEdited;onWand.write(wandEdited);
  if(wandEdited.str().find("ButtonMap0=5")==std::string::npos) return EXIT_FAILURE;

  // A VR.ini written by a Quest build, or hand-edited, reaches a PC through the
  // same reader: every value it carries has to come back inside its own range.
  Vr::Settings hostile;
  std::istringstream out_of_range("ButtonLayoutVersion=2\nButtonMap0=42\nButtonMap1=-7\n[VR]\nWorldScale=9\n"
                                  "RenderScale=0\nRunSpeed=-3\nHudDistance=99\nSnapAngle=1000\nTurnMode=17\nProfilerView=6\n");
  hostile.read(out_of_range);
  if(hostile.mapping[0]!=9 || hostile.mapping[1]!=0 || hostile.worldScale!=2.f || hostile.renderScale!=0.6f ||
     hostile.runSpeed!=0.75f || hostile.hudDistance!=3.f || hostile.snapAngle!=90 ||
     hostile.turn!=Vr::TurnMode::Snap || hostile.profilerView!=2) return EXIT_FAILURE;
  std::puts("Release defaults: 24 settings/calibration/persistence/button-map checks passed");
}
