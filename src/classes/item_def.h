#ifndef GTAEDITOR_ITEM_DEF_H
#define GTAEDITOR_ITEM_DEF_H

#include "tdfx.h"

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/vector.hpp>

#include <memory>

// Forward declaration
struct ColFile;

/// Item Definition parsed from IDE files (objs/tobj sections).
/// Describes a model's properties: name, texture dictionary, flags, etc.
struct ItemDef {
	godot::String model_name;
	godot::String txd_name;
	float render_distance = 0.0f;
	/// Secondary draw distances for multi-LOD IDE entries.
	float render_distance_2 = 0.0f;
	float render_distance_3 = 0.0f;
	int flags = 0;
	/// Number of draw distance values defined in the IDE file (1-3).
	int draw_distance_count = 1;
	/// Whether this item definition is a LOD model (identified by name prefix or flag).
	bool is_lod = false;
	godot::Vector<std::shared_ptr<TDFX>> children; // 2DFX effects
	std::shared_ptr<ColFile> colfile;               // Collision data (optional)
};

#endif // GTAEDITOR_ITEM_DEF_H
