#pragma once

#include "versioninfo.h"

namespace GameDetection {

struct Evidence {
  int  gothic1Score = 0;
  int  gothic2Score = 0;
  bool gothic1World = false;
  bool gothic2World = false;
  bool addonWorld = false;
  bool hasPatch = false;
  int  patch = 0;
  };

inline VersionInfo detect(const Evidence& e) {
  VersionInfo result;
  result.game = e.gothic1Score>e.gothic2Score ? 1 : 2;
  const bool gothic2 = e.gothic2World || e.addonWorld;
  // Both games can ship OU.BIN, and mobile packages may omit the original Gothic.ini.
  // Prefer unambiguous world contents over the legacy filename and settings hints.
  if(e.gothic1World!=gothic2)
    result.game = gothic2 ? 2 : 1;
  if(result.game==2)
    result.patch = e.hasPatch ? e.patch : (e.addonWorld ? 5 : 0);
  return result;
  }

}
