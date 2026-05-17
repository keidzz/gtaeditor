#ifndef MAP_MATERIAL_H
#define MAP_MATERIAL_H

#include "../rw/dff_parser.h"
#include "item_definition.h"
#include "texture_collection.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot {

// =============================================================================
// MapMaterial — Static helpers for creating Godot materials from GTA DFF data.
// =============================================================================

class MapMaterial {
public:
	// Create a StandardMaterial3D from DFF material data, applying textures and flags.
	static Ref<StandardMaterial3D> create(const DffMaterial &p_mat, const String &p_txd_name,
										  uint32_t p_flags, TextureCollection &textures);

	// Apply transparency settings based on alpha detection.
	static void apply_transparency(Ref<StandardMaterial3D> mat, bool is_transparent,
								   Image::AlphaMode alpha_mode, bool is_additive = false);
};

} // namespace godot

#endif // MAP_MATERIAL_H
