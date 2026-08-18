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
	// Not black by default — flat near-black on every slot made vehicles
	// hard to even look at in the editor before you'd manually set colors.
	// GTAVehicleInstance additionally exposes a `color_preset` property that
	// picks a model's color combo from data/carcols.dat (see
	// gta_vehicle_instance.cpp) for the game's actual paint colors.
	Color primary = Color(0.55f, 0.05f, 0.05f); // muted red
	Color secondary = Color(0.05f, 0.05f, 0.06f); // near-black
	Color tertiary = Color(0.05f, 0.05f, 0.06f);
	Color quaternary = Color(0.05f, 0.05f, 0.06f);
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
	// p_use_vertex_colors: GTA world geometry's baked lighting lives in the
	// mesh vertex colors, so world/props enable vertex_color_use_as_albedo;
	// vehicles do too (tires/glass/trim need it — the game multiplies vertex
	// colors into every vehicle material), only paint colors pass through
	// via p_paint.
	static Ref<StandardMaterial3D> create(const DffMaterial &p_mat, const String &p_txd_name,
										  uint32_t p_flags, TextureCollection &textures,
										  const VehiclePaintColors *p_paint = nullptr,
										  bool p_use_vertex_colors = true);

	// Apply transparency settings based on alpha detection.
	// is_transparent: material color alpha < 255 (alpha-blended, per the
	// game's CustomBuildingPipeline). alpha_mode: texture with real
	// transparent pixels (alpha-scissored, matching the game's alpha test).
	static void apply_transparency(Ref<StandardMaterial3D> mat, bool is_transparent,
								   Image::AlphaMode alpha_mode);
};

} // namespace godot

#endif // MAP_MATERIAL_H
