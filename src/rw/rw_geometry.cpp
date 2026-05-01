#include "rw_geometry.h"
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/surface_tool.hpp>
#include <godot_cpp/templates/hash_map.hpp>

// ── Parser ───────────────────────────────────────────────────────────────────

void RWGeometry::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_GEOMETRY, "Expected GEOMETRY chunk.");

	// Skip the struct sub-chunk header
	RWChunk struct_chunk;
	struct_chunk.read_header(file);

	format = file->get_32();
	tri_count = file->get_32();
	vert_count = file->get_32();
	morph_target_count = file->get_32();

	// Older versions store lighting parameters inline
	if (get_version() < 0x34000) {
		ambient = file->get_float();
		specular_val = file->get_float();
		diffuse = file->get_float();
	}

	// Only parse vertex data if this isn't a native-format geometry
	if (!(format & rpGEOMETRY_NATIVE)) {
		// Read prelit vertex colors (4 bytes per vertex)
		if (format & rpGEOMETRY_PRELIT) {
			prelit_colors.resize(vert_count);
			for (uint32_t v = 0; v < vert_count; v++) {
				float r = file->get_8() / 255.0f;
				float g = file->get_8() / 255.0f;
				float b = file->get_8() / 255.0f;
				float a = file->get_8() / 255.0f; // Read but ignore for color to avoid transparency issues
				prelit_colors.set(v, Color(r, g, b, 1.0f));
			}
		}

		// Determine UV layer count from format flags
		uv_count = (format & 0x00FF0000) >> 16;
		if (uv_count == 0) {
			if (format & rpGEOMETRY_TEXTURED2) {
				uv_count = 2;
			} else if (format & rpGEOMETRY_TEXTURED) {
				uv_count = 1;
			}
		}

		// Read UV coordinates for each layer
		uvs.resize(uv_count);
		for (uint32_t layer = 0; layer < uv_count; layer++) {
			PackedVector2Array &coords = uvs.write[layer];
			coords.resize(vert_count);
			for (uint32_t v = 0; v < vert_count; v++) {
				float u = file->get_float();
				float vv = file->get_float();
				coords.set(v, Vector2(u, vv));
			}
		}

		// Read triangle indices
		tris.resize(tri_count);
		for (uint32_t i = 0; i < tri_count; i++) {
			RWTriangle &tri = tris.write[i];
			tri.vertex_2 = file->get_16();
			tri.vertex_1 = file->get_16();
			tri.material_id = file->get_16();
			tri.vertex_3 = file->get_16();
		}
	}

	// Read morph targets (contains vertex positions and normals)
	morph_targets.resize(morph_target_count);
	for (uint32_t i = 0; i < morph_target_count; i++) {
		RWMorphTarget &mt = morph_targets.write[i];

		mt.bounding_sphere.x = file->get_float();
		mt.bounding_sphere.y = file->get_float();
		mt.bounding_sphere.z = file->get_float();
		mt.bounding_sphere.radius = file->get_float();

		mt.has_vertices = file->get_32() != 0;
		mt.has_normals = file->get_32() != 0;

		if (mt.has_vertices) {
			mt.vertices.resize(vert_count);
			for (uint32_t v = 0; v < vert_count; v++) {
				// Convert GTA coords (X,Y,Z) to Godot coords (X,Z,-Y)
				float x = file->get_float();
				float y = file->get_float();
				float z = file->get_float();
				mt.vertices.write[v] = Vector3(x, z, -y);
			}
		}

		if (mt.has_normals) {
			mt.normals.resize(vert_count);
			for (uint32_t v = 0; v < vert_count; v++) {
				float nx = file->get_float();
				float ny = file->get_float();
				float nz = file->get_float();
				mt.normals.write[v] = Vector3(nx, nz, ny);
			}
		}
	}

	// Parse the material list that follows the geometry data
	material_list.parse(file);

	// Skip any remaining extension chunks
	skip(file);
}

// ── Mesh builder ─────────────────────────────────────────────────────────────

Ref<ArrayMesh> RWGeometry::build_mesh() {
	ERR_FAIL_COND_V(morph_targets.size() == 0, Ref<ArrayMesh>());

	const RWMorphTarget &mt = morph_targets[0];
	if (!mt.has_vertices) {
		Ref<ArrayMesh> empty;
		empty.instantiate();
		return empty;
	}

	// Group triangles by material ID for efficient multi-surface rendering.
	// Using a HashMap to avoid repeated linear scans.
	HashMap<uint16_t, Vector<int>> surfaces; // material_id → triangle indices
	for (int i = 0; i < (int)tris.size(); i++) {
		uint16_t mat_id = tris[i].material_id;
		if (!surfaces.has(mat_id)) {
			surfaces.insert(mat_id, Vector<int>());
		}
		surfaces[mat_id].push_back(i);
	}

	Ref<ArrayMesh> mesh;

	// Build each surface (one per material)
	for (const KeyValue<uint16_t, Vector<int>> &kv : surfaces) {
		Ref<SurfaceTool> st;
		st.instantiate();
		st->begin(Mesh::PRIMITIVE_TRIANGLES);

		const Vector<int> &tri_indices = kv.value;

		for (int ti = 0; ti < tri_indices.size(); ti++) {
			const RWTriangle &tri = tris[tri_indices[ti]];

			// Emit vertices in reverse winding order (3, 2, 1) to match Godot's convention
			uint16_t verts[3] = { tri.vertex_3, tri.vertex_2, tri.vertex_1 };
			for (int vi = 0; vi < 3; vi++) {
				uint16_t idx = verts[vi];

				if (mt.has_normals && idx < (uint16_t)mt.normals.size()) {
					st->set_normal(mt.normals[idx]);
				}
				if (uvs.size() > 0 && idx < (uint16_t)uvs[0].size()) {
					st->set_uv(uvs[0][idx]);
				}
				if (prelit_colors.size() > 0 && idx < (uint16_t)prelit_colors.size()) {
					st->set_color(prelit_colors[idx]);
				}

				st->add_vertex(mt.vertices[idx]);
			}
		}

		// Set material for this surface
		const RWMaterial &rwmat = material_list.materials[kv.key];
		Ref<StandardMaterial3D> mat = rwmat.create_material(prelit_colors.size() > 0);

		if (rwmat.is_textured) {
			mat->set_meta("texture_name", rwmat.texture.texture_name);
		}
		st->set_material(mat);

		// Generate normals if not provided and not a tristrip
		if (!(format & rpGEOMETRY_TRISTRIP) && !mt.has_normals) {
			st->generate_normals();
		}

		// Commit surface to the mesh
		if (mesh.is_null()) {
			mesh = st->commit();
		} else {
			st->commit(mesh);
		}
	}

	return mesh;
}
