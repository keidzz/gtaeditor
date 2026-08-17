#include "gta_dff_geometry.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

namespace {

// [SOURCED] librw src/rwbase.h
constexpr uint32_t ID_STRUCT = 0x01;
constexpr uint32_t ID_EXTENSION = 0x03;
constexpr uint32_t ID_MATLIST = 0x08;
constexpr uint32_t ID_GEOMETRY = 0x0F;
constexpr uint32_t ID_CLUMP = 0x10;
constexpr uint32_t ID_FRAMELIST = 0x0E;
constexpr uint32_t ID_GEOMETRYLIST = 0x1A;
constexpr uint32_t ID_SKIN = (1u << 8) | 0x16; // MAKEPLUGINID(VEND_CRITERIONTK=1, 0x16) -> 0x116
constexpr uint32_t ID_NAOBJECT = 0x00;

// [SOURCED] librw src/rwobjects.h, Geometry::Flags
constexpr uint32_t GEOFLAG_TEXTURED = 0x04;
constexpr uint32_t GEOFLAG_PRELIT = 0x08;
constexpr uint32_t GEOFLAG_NORMALS = 0x10;
constexpr uint32_t GEOFLAG_TEXTURED2 = 0x80;
constexpr uint32_t GEOFLAG_NATIVE = 0x01000000;

struct Cursor {
	const uint8_t *data;
	size_t size;
	size_t pos = 0;
	bool ok = true;

	bool read_raw(void *dst, size_t n) {
		if (!ok || pos + n > size) {
			ok = false;
			return false;
		}
		std::memcpy(dst, data + pos, n);
		pos += n;
		return true;
	}

	template <typename T>
	bool read(T &out) {
		return read_raw(&out, sizeof(T));
	}

	bool skip(size_t n) {
		if (!ok || pos + n > size) {
			ok = false;
			return false;
		}
		pos += n;
		return true;
	}
};

struct ChunkHeader {
	uint32_t type = 0;
	uint32_t length = 0;
	uint32_t library_id = 0;
};

// [SOURCED] librw src/base.cpp readChunkHeaderInfo — 12 bytes: type, size, libraryID
bool read_chunk_header(Cursor &c, ChunkHeader &out) {
	return c.read(out.type) && c.read(out.length) && c.read(out.library_id);
}

// [SOURCED] librw src/base.cpp findChunk
bool find_chunk(Cursor &c, uint32_t type, ChunkHeader &out) {
	while (true) {
		ChunkHeader h;
		if (!read_chunk_header(c, h)) {
			return false;
		}
		if (h.type == ID_NAOBJECT) {
			return false;
		}
		if (h.type == type) {
			out = h;
			return true;
		}
		if (!c.skip(h.length)) {
			return false;
		}
	}
}

inline Vector3 convert_z_up_to_y_up(const Vector3 &v) {
	return Vector3(v.y, v.z, v.x);
}

} // namespace

GTADffGeometry::GTADffGeometry() {
}

GTADffGeometry::~GTADffGeometry() {
}

void GTADffGeometry::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse", "bytes"), &GTADffGeometry::parse);
	ClassDB::bind_method(D_METHOD("get_geometry_count"), &GTADffGeometry::get_geometry_count);
	ClassDB::bind_method(D_METHOD("get_vertex_count"), &GTADffGeometry::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &GTADffGeometry::get_triangle_count);
	ClassDB::bind_method(D_METHOD("get_skin_used_bones"), &GTADffGeometry::get_skin_used_bones);
	ClassDB::bind_method(D_METHOD("has_normals"), &GTADffGeometry::has_normals);
	ClassDB::bind_method(D_METHOD("has_uvs"), &GTADffGeometry::has_uvs);
	ClassDB::bind_method(D_METHOD("has_skin"), &GTADffGeometry::has_skin);
	ClassDB::bind_method(D_METHOD("get_skin_bone_count"), &GTADffGeometry::get_skin_bone_count);
	ClassDB::bind_method(D_METHOD("get_vertex_skin_weights", "vertex_index"), 
&GTADffGeometry::get_vertex_skin_weights);
	ClassDB::bind_method(D_METHOD("get_skin_inverse_bind_matrix", "bone_index"), &GTADffGeometry::get_skin_inverse_bind_matrix);
	ClassDB::bind_method(D_METHOD("build_array_mesh", "flip_winding"), &GTADffGeometry::build_array_mesh, DEFVAL(true));
}

bool GTADffGeometry::parse(const PackedByteArray &bytes) {
	vertices.clear();
	normals.clear();
	uvs.clear();
	triangle_indices.clear();
	triangle_material_ids.clear();
	geometry_count_in_file = 0;

	Cursor c{ bytes.ptr(), (size_t)bytes.size() };

	// CLUMP -> STRUCT (skip, don't need its fields here) -> FRAMELIST (skip
	// entirely, GTADffSkeleton handles that half) -> GEOMETRYLIST.
	ChunkHeader clump_h;
	if (!find_chunk(c, ID_CLUMP, clump_h)) {
		UtilityFunctions::push_error("GTADffGeometry: no CLUMP chunk found — not a valid .dff");
		return false;
	}

	ChunkHeader struct_h;
	if (!find_chunk(c, ID_STRUCT, struct_h) || !c.skip(struct_h.length)) {
		UtilityFunctions::push_error("GTADffGeometry: CLUMP has no STRUCT sub-chunk, or it's truncated");
		return false;
	}

	ChunkHeader framelist_h;
	if (!find_chunk(c, ID_FRAMELIST, framelist_h) || !c.skip(framelist_h.length)) {
		UtilityFunctions::push_error("GTADffGeometry: no FRAMELIST chunk found, or it's truncated");
		return false;
	}

	ChunkHeader geolist_h;
	if (!find_chunk(c, ID_GEOMETRYLIST, geolist_h)) {
		UtilityFunctions::push_error("GTADffGeometry: no GEOMETRYLIST chunk found — this .dff may have no mesh (skeleton-only file?)");
		return false;
	}

	ChunkHeader glstruct_h;
	if (!find_chunk(c, ID_STRUCT, glstruct_h)) {
		UtilityFunctions::push_error("GTADffGeometry: GEOMETRYLIST has no STRUCT sub-chunk");
		return false;
	}

	int32_t num_geometries = 0;
	if (!c.read(num_geometries) || num_geometries < 0) {
		UtilityFunctions::push_error("GTADffGeometry: could not read geometry count");
		return false;
	}
	geometry_count_in_file = num_geometries;

	if (num_geometries == 0) {
		UtilityFunctions::push_error("GTADffGeometry: GEOMETRYLIST is empty — this .dff has no mesh (skeleton-only file, e.g. player.dff might just be the rig; try a clothing-piece .dff instead)");
		return false;
	}

	ChunkHeader geo_h;
	if (!find_chunk(c, ID_GEOMETRY, geo_h)) {
		UtilityFunctions::push_error("GTADffGeometry: expected a GEOMETRY chunk inside GEOMETRYLIST");
		return false;
	}
	const size_t geo_end = c.pos + geo_h.length;

	// --- Inside the GEOMETRY chunk ---
	ChunkHeader gstruct_h;
	if (!find_chunk(c, ID_STRUCT, gstruct_h)) {
		UtilityFunctions::push_error("GTADffGeometry: GEOMETRY has no STRUCT sub-chunk");
		return false;
	}

	uint32_t flags = 0;
	int32_t num_triangles = 0;
	int32_t num_vertices = 0;
	int32_t num_morph_targets = 0;
	if (!(c.read(flags) && c.read(num_triangles) && c.read(num_vertices) && c.read(num_morph_targets))) {
		UtilityFunctions::push_error("GTADffGeometry: truncated GEOMETRY header");
		return false;
	}

	if (flags & GEOFLAG_NATIVE) {
		UtilityFunctions::push_error("GTADffGeometry: this geometry uses RenderWare's NATIVE (platform-specific) format, which isn't supported — unexpected for a GTA:SA PC asset, but not something this parser can read");
		return false;
	}
	if (num_vertices <= 0 || num_triangles < 0) {
		UtilityFunctions::push_error("GTADffGeometry: nonsensical vertex/triangle count");
		return false;
	}

	// [SOURCED] Geometry::create — numTexCoordSets derivation
	int32_t num_tex_coord_sets = (int32_t)((flags & 0xFF0000) >> 16);
	if (num_tex_coord_sets == 0) {
		if (flags & GEOFLAG_TEXTURED) {
			num_tex_coord_sets = 1;
		} else if (flags & GEOFLAG_TEXTURED2) {
			num_tex_coord_sets = 2;
		}
	}

	if (flags & GEOFLAG_PRELIT) {
		if (!c.skip((size_t)num_vertices * 4)) { // RGBA8 per vertex — not used yet
			UtilityFunctions::push_error("GTADffGeometry: truncated prelit color data");
			return false;
		}
	}

	// Only the FIRST texture coordinate set is kept (num_tex_coord_sets can
	// be up to 8 in the format, e.g. for multi-layer effects — not needed
	// for a basic textured mesh).
	uvs.resize((size_t)num_vertices);
	for (int32_t set_i = 0; set_i < num_tex_coord_sets; ++set_i) {
		for (int32_t v = 0; v < num_vertices; ++v) {
			float u, v_coord;
			if (!(c.read(u) && c.read(v_coord))) {
				UtilityFunctions::push_error("GTADffGeometry: truncated UV data");
				return false;
			}
			if (set_i == 0) {
				uvs[(size_t)v] = Vector2(u, v_coord);
			}
		}
	}
	if (num_tex_coord_sets == 0) {
		uvs.clear();
	}

	// [SOURCED] packed triangle format — NOT simply 3 sequential indices.
	triangle_indices.resize((size_t)num_triangles * 3);
	triangle_material_ids.resize((size_t)num_triangles);
	for (int32_t t = 0; t < num_triangles; ++t) {
		uint32_t tri0, tri1;
		if (!(c.read(tri0) && c.read(tri1))) {
			UtilityFunctions::push_error("GTADffGeometry: truncated triangle data");
			return false;
		}
		triangle_indices[(size_t)t * 3 + 0] = (int32_t)(tri0 >> 16);
		triangle_indices[(size_t)t * 3 + 1] = (int32_t)(tri0 & 0xFFFF);
		triangle_indices[(size_t)t * 3 + 2] = (int32_t)(tri1 >> 16);
		triangle_material_ids[(size_t)t] = (int32_t)(tri1 & 0xFFFF);
	}

	// [SOURCED] morph targets — GTA:SA meshes always have exactly 1 (no
	// morphing support was used), but loop generically anyway; only the
	// first target's data is kept.
	vertices.resize((size_t)num_vertices);
	bool got_positions = false;
	bool got_normals = false;
	for (int32_t m = 0; m < num_morph_targets; ++m) {
		if (!c.skip(4 * 4)) { // bounding sphere: center xyz + radius
			UtilityFunctions::push_error("GTADffGeometry: truncated morph target bounding sphere");
			return false;
		}
		int32_t has_vertices = 0;
		int32_t has_normals = 0;
		if (!(c.read(has_vertices) && c.read(has_normals))) {
			UtilityFunctions::push_error("GTADffGeometry: truncated morph target flags");
			return false;
		}
		if (has_vertices) {
			for (int32_t v = 0; v < num_vertices; ++v) {
				float x, y, z;
				if (!(c.read(x) && c.read(y) && c.read(z))) {
					UtilityFunctions::push_error("GTADffGeometry: truncated vertex position data");
					return false;
				}
				if (m == 0) {
					vertices[(size_t)v] = convert_z_up_to_y_up(Vector3(x, y, z));
					got_positions = true;
				}
			}
		}
		if (has_normals) {
			normals.resize((size_t)num_vertices);
			for (int32_t v = 0; v < num_vertices; ++v) {
				float x, y, z;
				if (!(c.read(x) && c.read(y) && c.read(z))) {
					UtilityFunctions::push_error("GTADffGeometry: truncated vertex normal data");
					return false;
				}
				if (m == 0) {
					normals[(size_t)v] = convert_z_up_to_y_up(Vector3(x, y, z));
					got_normals = true;
				}
			}
		}
	}
	if (!got_positions) {
		UtilityFunctions::push_error("GTADffGeometry: geometry has no vertex position data");
		return false;
	}
	if (!got_normals) {
		normals.clear();
	}
	(void)GEOFLAG_NORMALS; // documents intent; actual presence is driven by the morph target's own hasNormals flag, not this bit, per the source

	// --- Materials: skip entirely, not parsed yet (see class comment) ---
	ChunkHeader matlist_h;
	if (find_chunk(c, ID_MATLIST, matlist_h)) {
		c.skip(matlist_h.length); // best-effort; a missing/short MATLIST isn't fatal here
	}

	// --- Skin weights, if present (ID_SKIN inside this geometry's trailing EXTENSION) ---
	// [SOURCED] librw src/skin.cpp readSkin, src/rwplugins.h struct Skin.
	has_skin_data = false;
	skin_bone_count = 0;
	skin_bone_indices.clear();
	skin_bone_weights.clear();

	ChunkHeader ext_h;
	if (find_chunk(c, ID_EXTENSION, ext_h)) {
		const size_t ext_end = c.pos + ext_h.length;
		ChunkHeader skin_h;
		bool found_skin = false;
		while (c.pos < ext_end) {
			ChunkHeader h;
			if (!read_chunk_header(c, h) || c.pos + h.length > ext_end) {
				break;
			}
			if (h.type == ID_SKIN) {
				skin_h = h;
				found_skin = true;
				break;
			}
			if (!c.skip(h.length)) {
				break;
			}
		}

		if (found_skin) {
			const size_t skin_end = c.pos + skin_h.length;
			uint8_t num_bones_u8 = 0, num_used_bones_u8 = 0, num_weights_u8 = 0, unused_u8 = 0;
			if (c.read(num_bones_u8) && c.read(num_used_bones_u8) && c.read(num_weights_u8) && c.read(unused_u8)) {
				const bool old_format = (num_used_bones_u8 == 0);
				skin_bone_count = num_bones_u8;

				skin_used_bones.clear();
				bool skin_ok = true;
				if (!old_format) {
					skin_used_bones.resize((size_t)num_used_bones_u8);
					for (uint8_t k = 0; k < num_used_bones_u8 && skin_ok; ++k) {
						uint8_t b = 0;
						skin_ok = c.read(b);
						skin_used_bones[k] = b;
					}
				}

				if (skin_ok) {
					skin_bone_indices.resize((size_t)num_vertices * 4);
					for (int32_t v = 0; v < num_vertices && skin_ok; ++v) {
						for (int k = 0; k < 4; ++k) {
							uint8_t idx = 0;
							skin_ok = c.read(idx);
							skin_bone_indices[(size_t)v * 4 + k] = idx;
						}
					}
				}

				if (skin_ok) {
					skin_bone_weights.resize((size_t)num_vertices * 4);
					for (int32_t v = 0; v < num_vertices && skin_ok; ++v) {
						for (int k = 0; k < 4; ++k) {
							float w = 0.0f;
							skin_ok = c.read(w);
							skin_bone_weights[(size_t)v * 4 + k] = w;
						}
					}
				}
				// Inverse bind matrices: numBones * 16 floats each (the 4th
				// element of each 4-float group is the RwMatrix flags/pad).
				// [SOURCED] librw src/skin.cpp readSkin -- stored AFTER the
				// weights, before any split-data section. Godot's Skeleton3D
				// computes its own inverse-bind from each bone's rest
				// transform, but the game skins with THESE baked matrices, so
				// they're parsed (RAW space, unconverted) and exposed for the
				// validation that they agree with inverse(rest).
				if (skin_ok) {
					skin_inv_matrices.clear();
					skin_inv_matrices.reserve((size_t)num_bones_u8 * 12);
					for (uint8_t b = 0; b < num_bones_u8 && skin_ok; ++b) {
						float m[16] = { 0.0f };
						for (int k = 0; k < 16 && skin_ok; ++k) {
							skin_ok = c.read(m[k]);
						}
						if (skin_ok) {
							for (int k = 0; k < 3; ++k) {
								skin_inv_matrices.push_back(m[k * 4]);
								skin_inv_matrices.push_back(m[k * 4 + 1]);
								skin_inv_matrices.push_back(m[k * 4 + 2]);
							}
							skin_inv_matrices.push_back(m[12]);
							skin_inv_matrices.push_back(m[13]);
							skin_inv_matrices.push_back(m[14]);
						}
					}
				}
				// We stop here rather than parse further into the skin
				// chunk's split-data section.

				has_skin_data = skin_ok;
				if (!skin_ok) {
					UtilityFunctions::push_error("GTADffGeometry: truncated/malformed skin data, discarding");
					skin_bone_indices.clear();
					skin_bone_weights.clear();
					skin_inv_matrices.clear();
					skin_bone_count = 0;
				}
			}
			c.pos = skin_end; // safety net regardless of success
		}
	}

	c.pos = geo_end; // safety net for the whole geometry chunk, same pattern as GTADffSkeleton

	return true;
}

int GTADffGeometry::get_geometry_count() const {
	return (int)geometry_count_in_file;
}

int GTADffGeometry::get_vertex_count() const {
	return (int)vertices.size();
}

int GTADffGeometry::get_triangle_count() const {
	return (int)triangle_indices.size() / 3;
}

PackedInt32Array GTADffGeometry::get_skin_used_bones() const {
	PackedInt32Array out;
	out.resize((int)skin_used_bones.size());
	for (size_t i = 0; i < skin_used_bones.size(); ++i) {
		out[(int)i] = (int32_t)skin_used_bones[i];
	}
	return out;
}

bool GTADffGeometry::has_normals() const {
	return !normals.empty();
}

bool GTADffGeometry::has_uvs() const {
	return !uvs.empty();
}

bool GTADffGeometry::has_skin() const {
	return has_skin_data;
}

int GTADffGeometry::get_skin_bone_count() const {
	return (int)skin_bone_count;
}

Array GTADffGeometry::get_vertex_skin_weights(int vertex_index) const {
	Array out;
	if (!has_skin_data || vertex_index < 0 || (size_t)vertex_index * 4 + 3 >= skin_bone_indices.size()) {
		return out;
	}
	for (int k = 0; k < 4; ++k) {
		Dictionary d;
		d["bone"] = skin_bone_indices[(size_t)vertex_index * 4 + k];
		d["weight"] = skin_bone_weights[(size_t)vertex_index * 4 + k];
		out.push_back(d);
	}
	return out;
}

PackedFloat32Array GTADffGeometry::get_skin_inverse_bind_matrix(int bone_index) const {
	PackedFloat32Array out;
	if (!has_skin_data || bone_index < 0 || (size_t)bone_index * 12 + 11 >= skin_inv_matrices.size()) {
		return out;
	}
	out.resize(12);
	for (int k = 0; k < 12; ++k) {
		out[k] = skin_inv_matrices[(size_t)bone_index * 12 + k];
	}
	return out;
}

Ref<ArrayMesh> GTADffGeometry::build_array_mesh(bool flip_winding) const {
	if (vertices.empty()) {
		UtilityFunctions::push_error("GTADffGeometry: build_array_mesh() called with no parsed geometry");
		return Ref<ArrayMesh>();
	}

	PackedVector3Array pv;
	pv.resize((int)vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i) {
		pv[(int)i] = vertices[i];
	}

	PackedVector3Array pn;
	if (!normals.empty()) {
		pn.resize((int)normals.size());
		for (size_t i = 0; i < normals.size(); ++i) {
			pn[(int)i] = normals[i];
		}
	}

	PackedVector2Array puv;
	if (!uvs.empty()) {
		puv.resize((int)uvs.size());
		for (size_t i = 0; i < uvs.size(); ++i) {
			puv[(int)i] = uvs[i];
		}
	}

	PackedInt32Array indices;
	indices.resize((int)triangle_indices.size());
	const size_t tri_count = triangle_indices.size() / 3;
	for (size_t t = 0; t < tri_count; ++t) {
		int32_t a = triangle_indices[t * 3 + 0];
		int32_t b = triangle_indices[t * 3 + 1];
		int32_t d = triangle_indices[t * 3 + 2];
		if (flip_winding) {
			indices[(int)(t * 3 + 0)] = a;
			indices[(int)(t * 3 + 1)] = d;
			indices[(int)(t * 3 + 2)] = b;
		} else {
			indices[(int)(t * 3 + 0)] = a;
			indices[(int)(t * 3 + 1)] = b;
			indices[(int)(t * 3 + 2)] = d;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = pv;
	if (!pn.is_empty()) {
		arrays[Mesh::ARRAY_NORMAL] = pn;
	}
	if (!puv.is_empty()) {
		arrays[Mesh::ARRAY_TEX_UV] = puv;
	}
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}
