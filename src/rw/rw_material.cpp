#include "rw_material.h"

void RWMaterial::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_MATERIAL, "Expected MATERIAL chunk.");

	// Read the struct sub-chunk header
	RWChunk struct_chunk;
	struct_chunk.read_header(file);

	file->get_32(); // flags (unused)

	// Read RGBA color
	color.r = file->get_8() / 255.0f;
	color.g = file->get_8() / 255.0f;
	color.b = file->get_8() / 255.0f;
	color.a = file->get_8() / 255.0f;

	file->get_32(); // unused

	is_textured = file->get_32() > 0;

	// Lighting properties (only in version > 0x30400)
	uint32_t ver = get_version();
	if (ver > 0x30400) {
		ambient  = file->get_float();
		specular = file->get_float();
		diffuse  = file->get_float();
	}

	// Parse texture reference if this material is textured
	if (is_textured) {
		texture.parse(file);
	}

	// Skip remaining extension data
	skip(file);
}

Ref<StandardMaterial3D> RWMaterial::create_material(bool use_vertex_colors) const {
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_albedo(color);

	if (use_vertex_colors) {
		mat->set_flag(StandardMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	}

	// Apply roughness from specular for newer versions
	if (get_version() > 0x30400) {
		mat->set_roughness(1.0f - specular);
	}

	return mat;
}
