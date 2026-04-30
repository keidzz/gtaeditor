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
	int lod_index = -1; // -1 = no LOD parent, otherwise index to parent object
};

#endif // GTAEDITOR_ITEM_PLACEMENT_H
