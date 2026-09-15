#pragma once

#include <string>
#include <string_view>

namespace CameraObstruction {

inline bool foliageTexture(std::string_view texture) {
  texture=texture.substr(texture.find_last_of("/\\")+1);
  std::string name(texture);
  for(char& c:name)
    if(c>='a' && c<='z') c=char(c-'a'+'A');

  // Texture families shared by the original G1/G2 assets.
  // Do not match generic GRASS or TREE: terrain, roofs and bark use those names too.
  constexpr std::string_view families[]={
    "MOWOBUSH", "MOWOFERN", "MOWOREED", "MOWORICEPLANT", "MOWOTREETOP", "MOWOLIANA",
    "OWDISPIDERWEB", "DECAL_MISC_SPIDERWEB", "OW_NATURE_BUSH_",
    "NW_MISC_GRASS_SPITZEN_", "NW_NATURE_FARN_", "NW_NATURE_GRASS_",
    "NW_NATURE_HOUSEGRASS_DECAL_", "NW_NATURE_LEAVE_", "NW_NATURE_LEAVES_", "NW_NATURE_BRANCH_",
    "NW_NATURE_PLANT_", "NW_NATURE_SWEETGRASS_", "NW_NATURE_GREATSEA_PLANT_",
    "NW_MISC_SEEROSE_", "NW_MISC_DUCKWEED_",
    "NW_NATURE_TREE_NEEDLE_", "NW_SEQ_NATURE_PINE_",
    "NW_SEQ_NATURE_CANYONBUSH_", "NW_SEQ_NATURE_SMALLBUSH_", "NW_SEQ_NATURE_SMALLPLANT_"
    };
  for(auto prefix:families)
    if(name.starts_with(prefix)) return true;
  return false;
  }

}
