#include "map_material.h"

namespace godot {

void MapMaterial::apply_transparency(Ref<StandardMaterial3D> mat, bool is_transparent, Image::AlphaMode alpha_mode, bool is_additive) {
	mat->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
	mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY);

	if (is_additive) {
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
	} else if (alpha_mode != Image::ALPHA_NONE) {
		// Texture has alpha (trees, fences, etc.) — use scissor so shadows cast correctly
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		mat->set_alpha_scissor_threshold(0.5f);
	} else if (is_transparent) {
		// Material color has alpha < 1 but texture has no alpha
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_ALWAYS);
	}
}

Ref<StandardMaterial3D> MapMaterial::create(const DffMaterial &p_mat, const String &p_txd_name,
		uint32_t p_flags, TextureCollection &textures) {
	Ref<StandardMaterial3D> mat;
	mat.instantiate();

	// Set base color from DFF material.
	mat->set_albedo(p_mat.color);

	// Culling.
	if (p_flags & FLAG_FACE_CULLING_OFF) {
		mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	} else {
		mat->set_cull_mode(BaseMaterial3D::CULL_BACK);
	}

	// Apply texture if the material is textured.
	Image::AlphaMode alpha_mode = Image::ALPHA_NONE;

	if (p_mat.textured && !p_mat.texture_name.is_empty()) {
		Ref<ImageTexture> tex;
		bool has_alpha = false;
		if (textures.get_texture(p_txd_name, p_mat.texture_name, tex, has_alpha)) {
			mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);

			if (has_alpha) {
				alpha_mode = Image::ALPHA_BIT;
			}
		}
	}

	// Apply transparency using proper alpha detection.
	bool is_transparent = (p_flags & FLAG_DRAW_LAST) || (p_mat.color.a < 1.0f);
	bool is_additive = (p_flags & FLAG_ALPHA_TRANSPARENCY);
	apply_transparency(mat, is_transparent, alpha_mode, is_additive);

	return mat;
}

} // namespace godot
