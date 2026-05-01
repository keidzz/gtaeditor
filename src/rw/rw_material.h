#ifndef GTAEDITOR_RW_MATERIAL_H
#define GTAEDITOR_RW_MATERIAL_H

#include "rw_chunk.h"
#include "rw_texture.h"
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

/// Parses an RW MATERIAL chunk containing color, lighting properties,
/// and an optional texture reference.
class RWMaterial : public RWChunk {
public:
	Color color;
	bool is_textured = false;
	RWTexture texture;

	float ambient  = 0.0f;
	float specular = 0.0f;
	float diffuse  = 0.0f;

	/// Parse a MATERIAL chunk from the current file position.
	void parse(Ref<FileAccess> file);

	/// Create a new StandardMaterial3D from the parsed data.
	/// Each call returns a NEW material instance (needed for per-surface materials).
	Ref<StandardMaterial3D> create_material(bool use_vertex_colors = false) const;
};

#endif // GTAEDITOR_RW_MATERIAL_H
