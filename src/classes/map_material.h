#ifndef MAP_MATERIAL_H
#define MAP_MATERIAL_H

#include "../rw/dff_parser.h"
#include "item_definition.h"
#include "texture_collection.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot {

// =============================================================================
// VehiclePaintColors — the four colors GTA substitutes onto a vehicle's
// paintable materials at runtime (from carcols.dat, normally). GTAVehicleInstance
// exposes these as Inspector properties and passes them into MapMaterial::create().
// =============================================================================

struct VehiclePaintColors {
	Color primary = Color(0.15f, 0.15f, 0.15f);
	Color secondary = Color(0.15f, 0.15f, 0.15f);
	Color tertiary = Color(0.15f, 0.15f, 0.15f);
	Color quaternary = Color(0.15f, 0.15f, 0.15f);
};

// =============================================================================
// MapMaterial — Static helpers for creating Godot materials from GTA DFF data.
// =============================================================================

class MapMaterial {
public:
	// Create a StandardMaterial3D from DFF material data, applying textures and flags.
	// p_paint is optional: when non-null, any of the four known reserved
	// "paintable" material colors (see gtamods.com/wiki/Carcols.dat) is
	// substituted with the matching color from p_paint instead of being
	// rendered literally. Only ever passed by GTAVehicleInstance — regular
	// map-prop rendering (MapBuilder, GTAModelInstance) is unaffected.
	static Ref<StandardMaterial3D> create(const DffMaterial &p_mat, const String &p_txd_name,
										  uint32_t p_flags, TextureCollection &textures,
										  const VehiclePaintColors *p_paint = nullptr);

	// Apply transparency settings based on alpha detection.
	static void apply_transparency(Ref<StandardMaterial3D> mat, bool is_transparent,
								   Image::AlphaMode alpha_mode, bool is_additive = false);
};

} // namespace godot

#endif // MAP_MATERIAL_H
