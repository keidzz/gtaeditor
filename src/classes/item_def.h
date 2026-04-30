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
	int flags = 0;
	godot::Vector<std::shared_ptr<TDFX>> children; // 2DFX effects
	std::shared_ptr<ColFile> colfile;               // Collision data (optional)
};

#endif // GTAEDITOR_ITEM_DEF_H
