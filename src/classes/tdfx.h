#ifndef GTAEDITOR_TDFX_H
#define GTAEDITOR_TDFX_H

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

/// Base class for 2DFX effects attached to item definitions.
/// Stores the parent item ID, local position offset, and color.
struct TDFX {
	int parent = 0;
	godot::Vector3 position;
	godot::Color color;

	virtual ~TDFX() = default;
};

#endif // GTAEDITOR_TDFX_H
