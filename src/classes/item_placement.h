#ifndef GTAEDITOR_ITEM_PLACEMENT_H
#define GTAEDITOR_ITEM_PLACEMENT_H

#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

/// Represents a placed instance of an item definition in the GTA world.
/// Parsed from IPL (Item Placement List) files - both text and binary formats.
struct ItemPlacement {
	int id = 0;
	godot::String model_name;
	int interior = 0;
	godot::Vector3 position;
	godot::Vector3 scale = godot::Vector3(1.0f, 1.0f, 1.0f);
	godot::Quaternion rotation;
	/// Index into this IPL group of the LOD (low-detail) child for this placement.
/// -1 = this placement has no LOD child.
/// Populated during parse; only meaningful before LOD-linking pass runs.
	int lod_index = -1;
	/// Index of the LOD child (lower-detail version) of this placement (-1 = none).
	/// Populated after all placements are loaded by the LOD linking pass.
	int lod_child_index = -1;
	/// Whether this placement is a LOD model (low-detail version).
	bool is_lod = false;
};

#endif // GTAEDITOR_ITEM_PLACEMENT_H
