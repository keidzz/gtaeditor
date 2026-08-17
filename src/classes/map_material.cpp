#include "map_material.h"

namespace godot {

void MapMaterial::apply_transparency(Ref<StandardMaterial3D> mat, bool is_transparent, Image::AlphaMode alpha_mode) {
	mat->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
	mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY);

	if (is_transparent) {
		// Material color alpha < 255: the game renders these meshes with
		// SRCALPHA/INVSRCALPHA blending (CustomBuildingPipeline.cpp:155).
		// Windows, glass, and signs are all textured — the material's own
		// alpha, not the texture's, is what makes them see-through.
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_ALWAYS);
	} else if (alpha_mode != Image::ALPHA_NONE) {
		// Opaque material color + a texture that really has transparent
		// pixels: the game's global alpha test (ref 140 in exteriors,
		// Renderer.cpp:349) cuts these out — trees, fences, foliage.
		// Scissor is used so the cutout still casts correct shadows.
		// The threshold matches the game's 140/255 test reference.
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		mat->set_alpha_scissor_threshold(140.0f / 255.0f);
	}
}

Ref<StandardMaterial3D> MapMaterial::create(const DffMaterial &p_mat, const String &p_txd_name,
		uint32_t p_flags, TextureCollection &textures, const VehiclePaintColors *p_paint,
		bool p_use_vertex_colors) {
	Ref<StandardMaterial3D> mat;
	mat.instantiate();

	// GTA world/prop geometry carries its lighting baked into per-vertex
	// colors (the game multiplies texture x vertex color; its directional
	// sun only lights dynamic entities). Rendering those colors as albedo
	// recovers the game's baked shading (AO, sun side, interior darkening)
	// that otherwise reads as flat, uniformly-lit surfaces. Vehicles are
	// excluded (p_use_vertex_colors = false): their paint is tuned for
	// real-time lighting and most of their vertices are near-white anyway.
	if (p_use_vertex_colors) {
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	}

	// Set base color from DFF material, substituting GTA's reserved
	// "special purpose" placeholder colors when this is a vehicle part
	// (p_paint != nullptr, only ever passed by GTAVehicleInstance):
	//  - 4 paintable-surface colors, swapped for this instance's actual
	//    paint (see gtamods.com/wiki/Carcols.dat).
	//  - a handful of reserved vehicle-light colors (headlight/taillight/
	//    indicator/reverse/fog materials, plus day/night visibility
	//    markers) that the game recognizes by these exact values and
	//    replaces with an actual glow/corona effect at runtime. We don't
	//    implement that runtime behavior (day/night state, corona
	//    sprites, brake/indicator state), so these are swapped for a
	//    plausible static lens color instead of rendering the literal
	//    marker color, which otherwise shows up as jarring green/cyan/blue.
	Color base_color = p_mat.color;
	if (p_paint != nullptr) {
		static const Color kPaintSlots[4] = {
			Color(60 / 255.0f, 255 / 255.0f, 0 / 255.0f), // primary   (#3cff00)
			Color(255 / 255.0f, 0 / 255.0f, 175 / 255.0f), // secondary (#ff00af)
			Color(0 / 255.0f, 255 / 255.0f, 255 / 255.0f), // tertiary  (#00ffff)
			Color(255 / 255.0f, 0 / 255.0f, 255 / 255.0f), // quaternary(#ff00ff)
		};
		const Color kPaintOverrides[4] = { p_paint->primary, p_paint->secondary, p_paint->tertiary, p_paint->quaternary };

		bool matched = false;
		for (int i = 0; i < 4; i++) {
			if (base_color.is_equal_approx(kPaintSlots[i])) {
				base_color = kPaintOverrides[i];
				base_color.a = p_mat.color.a;
				matched = true;
				break;
			}
		}

		if (!matched) {
			// Reserved light-marker colors -> plausible static lens color.
			// Approximate values (community-documented, may vary slightly
			// from vanilla byte-for-byte) — close enough to catch the
			// intent even if a couple don't match exactly on your files.
			struct LightSlot {
				Color marker;
				Color replacement;
			};
			static const LightSlot kLightSlots[] = {
				{ Color(184 / 255.0f, 255 / 255.0f, 0), Color(0.6f, 0.05f, 0.05f) }, // brake (left)  -> red lens
				{ Color(255 / 255.0f, 59 / 255.0f, 0), Color(0.6f, 0.05f, 0.05f) }, // brake (right) -> red lens
				{ Color(255 / 255.0f, 173 / 255.0f, 0), Color(0.85f, 0.85f, 0.8f) }, // reverse       -> clear/white lens
				{ Color(0, 255 / 255.0f, 198 / 255.0f), Color(0.85f, 0.85f, 0.8f) }, // reverse       -> clear/white lens
				{ Color(183 / 255.0f, 255 / 255.0f, 0), Color(0.7f, 0.35f, 0.05f) }, // indicator     -> amber lens
				{ Color(255 / 255.0f, 58 / 255.0f, 0), Color(0.7f, 0.35f, 0.05f) }, // indicator     -> amber lens
				{ Color(0, 16 / 255.0f, 1.0f), Color(0.9f, 0.9f, 0.85f) }, // night-only marker    -> neutral (headlight-ish)
				{ Color(0, 17 / 255.0f, 1.0f), Color(0.9f, 0.9f, 0.85f) }, // all-day marker       -> neutral
				{ Color(0, 18 / 255.0f, 1.0f), Color(0.9f, 0.9f, 0.85f) }, // day-only marker      -> neutral
			};
			for (const LightSlot &slot : kLightSlots) {
				if (base_color.is_equal_approx(slot.marker)) {
					base_color = slot.replacement;
					base_color.a = p_mat.color.a;
					matched = true;
					break;
				}
			}
		}

		// Glass fallback: GTA's vehicle windows look dark/tinted in-game
		// via an environment-map/reflection shader trick at render time —
		// the material's own raw diffuse color is often a neutral light
		// grey/white, meant only as a faint base tint under that
		// reflection. Since we don't replicate that shader, applying the
		// raw color literally produces flat, light "white glass" instead
		// of the tinted look players expect. Any remaining untextured,
		// already-translucent vehicle material (i.e. not paint, not a
		// light lens — glass is basically what's left) gets a plausible
		// dark tinted-glass color instead.
		if (!matched && !p_mat.textured && p_mat.color.a < 0.95f) {
			base_color = Color(0.05f, 0.08f, 0.1f, MIN(p_mat.color.a, 0.45f));
			mat->set_roughness(0.05f);
			mat->set_metallic(0.3f);
		}
	}
	mat->set_albedo(base_color);

	// Culling.
	if (p_flags & FLAG_FACE_CULLING_OFF) {
		mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	} else {
		mat->set_cull_mode(BaseMaterial3D::CULL_BACK);
	}

	// Apply texture if the material is textured.
	Image::AlphaMode alpha_mode = Image::ALPHA_NONE;
	bool has_texture = false;

	if (p_mat.textured && !p_mat.texture_name.is_empty()) {
		Ref<ImageTexture> tex;
		bool has_alpha_content = false;
		if (textures.get_texture(p_txd_name, p_mat.texture_name, tex, has_alpha_content)) {
			mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
			has_texture = true;

			if (has_alpha_content) {
				alpha_mode = Image::ALPHA_BIT;
			}
		}
	}

	// Transparency, following the real game's rules (verified against
	// gta-reversed): a mesh is alpha-blended iff its material color alpha
	// is < 255 — texture or not. An opaque material whose texture really
	// contains transparent pixels is alpha-scissored instead (the game's
	// global alpha test with ref 140 exterior, 100 in the alpha pass).
	// The IDE flags are NOT transparency switches: FLAG_DRAW_LAST (0x4)
	// only moves the entity into the sorted alpha draw list (back-to-front
	// draw order), and FLAG_ALPHA_TRANSPARENCY (0x8) is only used by the
	// game while LOD-distance-fading, never in the normal render pass.
	bool is_transparent = p_mat.color.a < 1.0f;
	apply_transparency(mat, is_transparent, alpha_mode);

	return mat;
}

} // namespace godot
