#ifndef MAP_WATER_LOADER_H
#define MAP_WATER_LOADER_H

#include "water_parser.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/noise_texture2d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

// =============================================================================
// MapWaterLoader — Builds a single merged MeshInstance3D from parsed water planes.
// =============================================================================

class MapWaterLoader {
public:
	// Build a MeshInstance3D containing all water planes as a single mesh.
	// Returns nullptr if water_planes is empty.
	static MeshInstance3D *build_water_mesh(const Vector<WaterPlane> &water_planes);
};

} // namespace godot

#endif // MAP_WATER_LOADER_H
