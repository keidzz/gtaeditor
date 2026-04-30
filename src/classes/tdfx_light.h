#ifndef GTAEDITOR_TDFX_LIGHT_H
#define GTAEDITOR_TDFX_LIGHT_H

#include "tdfx.h"

/// 2DFX light effect (type 0). Extends TDFX with light-specific properties.
struct TDFXLight : public TDFX {
	float render_distance = 0.0f;
	float light_range = 0.0f; // Named 'light_range' to avoid conflict with std::range
	int shadow_intensity = 0;
};

#endif // GTAEDITOR_TDFX_LIGHT_H
