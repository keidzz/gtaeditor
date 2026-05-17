#ifndef ITEM_PLACEMENT_H
#define ITEM_PLACEMENT_H

#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

// =============================================================================
// ItemPlacement — A single object instance in the world, parsed from IPL files.
// Positions and rotations are already converted to Godot's coordinate system
// during parsing.
//
// Coordinate conversion (applied during IPL parsing):
//   Godot position = (GTA.x, GTA.z, -GTA.y)
//   Godot quaternion = (-GTA.rx, -GTA.rz, -GTA.ry, GTA.rw)
// =============================================================================

struct ItemPlacement {
	int32_t definition_id = 0; // Links to ItemDefinition.id
	String item_name; // Model name from IPL (or "streaming" for binary)
	Vector3 position; // World position (Godot coordinates)
	Quaternion rotation; // World rotation (Godot coordinates)
	int32_t interior = 0; // Interior ID (0 = exterior)
	int32_t lod_index = -1; // Index into placement array for LOD version
	float lod_begin_distance = -1.0f; // Minimum distance at which this LOD model becomes visible
	float draw_distance = 300.0f; // Cached draw distance from definition
};

#endif // ITEM_PLACEMENT_H
