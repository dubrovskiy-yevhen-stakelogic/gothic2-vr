#pragma once
#include <Tempest/Matrix4x4>

namespace Vr {
// Both matrices use the renderer's camera-relative coordinates. Form only
// the small inter-frame displacement, never invert absolute world translation.
inline Tempest::Matrix4x4 taaReprojection(const Tempest::Matrix4x4& previous,
                                        const Tempest::Matrix4x4& current,
                                        const Tempest::Vec3& previousOrigin,
                                        const Tempest::Vec3& currentOrigin) {
  if(previous==current && previousOrigin==currentOrigin)
    return Tempest::Matrix4x4::mkIdentity();
  auto inverse=current;
  inverse.inverse();
  auto shifted=previous;
  shifted.translate(currentOrigin-previousOrigin);
  return shifted*inverse;
}
}
