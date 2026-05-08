#ifndef ITEM_DEFINITION_H
#define ITEM_DEFINITION_H

#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// IDE Definition Flags — Bitfield from the last token of OBJS/TOBJ lines.
// Reference: http://www.gtamodding.com/wiki/Item_Definition#Flags
// =============================================================================

enum DefinitionFlags : uint32_t {
	FLAG_NONE = 0,
	FLAG_WET_EFFECT = 1 << 0,
	FLAG_NIGHT_OBJECT = 1 << 1,
	FLAG_DRAW_LAST = 1 << 2,        // Transparent, render after opaque
	FLAG_ALPHA_TRANSPARENCY = 1 << 3, // Additive blending (needs NIGHT_OBJECT)
	FLAG_DAY_FLAG = 1 << 4,
	FLAG_INTERIOR_OBJECT = 1 << 5,
	FLAG_SHADOWS = 1 << 6,           // Shadow mesh — disable rendering
	FLAG_CULL_OFF = 1 << 7,          // Don't cull this object's surface
	FLAG_DRAW_DIST_OFF = 1 << 8,     // Disable draw distance (LOD objects)
	FLAG_BREAKABLE = 1 << 9,
	FLAG_BREAKABLE_CRACKS = 1 << 10,
	FLAG_GARAGE_DOORS = 1 << 11,
	FLAG_TWO_CLUMP = 1 << 12,
	FLAG_SMALL_FLORA = 1 << 13,
	FLAG_STANDARD_FLORA = 1 << 14,
	FLAG_TIMECYCLE_POLE_SHADOW = 1 << 15,
	FLAG_EXPLOSIVE = 1 << 16,
	FLAG_GRAFFITI = 1 << 20,
	FLAG_FACE_CULLING_OFF = 1 << 21, // Draw both sides of polygons
};

// =============================================================================
// ItemDefinition — Parsed from IDE files (OBJS, TOBJ, ANIM sections).
// Maps a numeric ID to model name, texture name, draw distance, and flags.
// =============================================================================

struct ItemDefinition {
	int32_t id = 0;
	String model_name;  // DFF filename without extension
	String txd_name;    // TXD filename without extension
	float draw_distance = 300.0f;
	uint32_t flags = 0;

	// TOBJ time-of-day fields (-1 = always visible)
	int32_t time_on = -1;
	int32_t time_off = -1;
};

// =============================================================================
// TextureParent — Parsed from IDE TXDP section.
// Links a child TXD to a parent TXD for texture fallback lookups.
// =============================================================================

struct TextureParent {
	String child_name;
	String parent_name;
};

#endif // ITEM_DEFINITION_H
