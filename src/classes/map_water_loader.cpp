#include "map_water_loader.h"

namespace godot {

// UV tiling: world units per full noise-texture repeat. Smaller = smaller,
// more numerous ripples; larger = bigger, gentler swells. Tuned for
// GTA:SA's world scale (units are roughly meters).
static const float WATER_UV_TILE = 1.0f / 12.0f;

MeshInstance3D *MapWaterLoader::build_water_mesh(const Vector<WaterPlane> &water_planes) {
	if (water_planes.size() == 0) {
		return nullptr;
	}

	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedInt32Array indices;
	int idx = 0;

	// Water planes are always flat/horizontal in GTA:SA, so a constant
	// up-normal is correct (no need to compute per-triangle cross products).
	const Vector3 up(0, 1, 0);

	auto push_vertex = [&](const Vector3 &p) {
		vertices.push_back(p);
		normals.push_back(up);
		// World-space UVs so the ripple normal map tiles continuously across
		// every plane instead of stretching one full repeat per plane.
		uvs.push_back(Vector2(p.x, p.z) * WATER_UV_TILE);
	};

	for (int i = 0; i < water_planes.size(); i++) {
		const WaterPlane &wp = water_planes[i];
		if (wp.is_triangle) {
			push_vertex(wp.p1);
			push_vertex(wp.p2);
			push_vertex(wp.p3);
			indices.push_back(idx++);
			indices.push_back(idx++);
			indices.push_back(idx++);
		} else {
			push_vertex(wp.p1);
			push_vertex(wp.p2);
			push_vertex(wp.p3);
			push_vertex(wp.p4);
			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx + 2);
			indices.push_back(idx + 3);
			indices.push_back(idx);
			idx += 4;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> water_mesh;
	water_mesh.instantiate();
	water_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

	// Procedurally-generated ripple normal map — no external texture asset
	// needed (consistent with not shipping any GTA assets), just noise
	// interpreted as a bump map. This is what actually breaks up the "flat
	// tinted glass" look; without it a StandardMaterial3D water plane has no
	// surface detail to catch light or distort what's visible through it.
	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise->set_frequency(0.15f);

	Ref<NoiseTexture2D> ripple_tex;
	ripple_tex.instantiate();
	ripple_tex->set_width(256);
	ripple_tex->set_height(256);
	ripple_tex->set_seamless(true);
	ripple_tex->set_as_normal_map(true);
	ripple_tex->set_bump_strength(6.0f);
	ripple_tex->set_noise(noise);

	Ref<StandardMaterial3D> water_mat;
	water_mat.instantiate();
	water_mat->set_albedo(Color(0.12f, 0.35f, 0.55f, 0.8f));
	water_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	// Water is visible from underwater / any angle, not just from above.
	water_mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	// Glossy, not matte — real water surfaces catch strong specular
	// highlights even though the surface itself isn't a metal.
	water_mat->set_roughness(0.05f);
	water_mat->set_metallic(0.05f);
	water_mat->set_specular(0.9f);
	water_mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, ripple_tex);
	water_mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
	water_mat->set_normal_scale(0.6f);
	water_mesh->surface_set_material(0, water_mat);

	MeshInstance3D *water_instance = memnew(MeshInstance3D);
	water_instance->set_mesh(water_mesh);
	water_instance->set_name("WaterPlanes");

	return water_instance;
}

} // namespace godot
