#pragma once

#include "feature_flags.h"

#if HELPERBYORC_TARGET_DIAGNOSTICS
#include <d3d9.h>

class CVector;
class CPed;
class CEntity;

namespace targetdiag {

bool PrepareNativeRaycast(IDirect3DDevice9* device, const char*& reason);
bool NativePedRaycast(const CVector& origin, const CVector& target,
    CPed* localPed, CEntity*& hit, const char*& failure);

} // namespace targetdiag
#endif
