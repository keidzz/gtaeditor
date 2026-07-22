#include "dff_parser.h"

#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// Internal data structures used during DFF parsing
// =============================================================================

struct ParsedGeometry {
	Vector<Vector3> vertices;
	Vector<Vector3> normals;
	Vector<Vector2> uvs;
	Vector<Color> colors;
	Vector<DffMaterial> materials;
	Vector<Dff2dfxLight> lights;

	// BinMesh sub-mesh data: indices grouped by material.
	struct SubMesh {
		int material_index = 0;
		Vector<int32_t> indices;
	};
	Vector<SubMesh> sub_meshes;

	// Fallback triangle data (used if no BinMesh section).
	Vector<int32_t> fallback_triangles;
	bool has_bin_mesh = false;
};

struct ParsedFrame {
	String name;
	Vector3 position;
	int32_t parent_index = -1;
	int32_t geometry_index = -1; // Set by Atomic section
};

// =============================================================================
// DffParser internal — recursive section processor
// =============================================================================

class DffParserInternal {
public:
	BinaryReader reader;
	Vector<ParsedGeometry> geometries;
	Vector<ParsedFrame> frames;
	int current_frame_name = 0;
	bool is_alpha_mask_string = false;

	void process_section(const RWSectionHeader &parent);
	void parse_struct(const RWSectionHeader &header, RWSectionType parent_type);
	void parse_frames_list();
	void parse_geometry(const RWSectionHeader &header);
	void parse_atomic();
	void parse_material();
	void parse_bin_mesh();
	void parse_2dfx(int32_t size);
	void parse_frame_name(int32_t size);
	void parse_string_section(int32_t size, RWSectionType parent_type);
};

// =============================================================================
// Recursive section processor — walks the RW binary tree
// =============================================================================

void DffParserInternal::process_section(const RWSectionHeader &parent) {
	int64_t end = reader.get_position() + parent.size;

	while (reader.get_position() < end) {
		RWSectionHeader header = reader.read_section_header();
		int64_t section_end = reader.get_position() + header.size;

		switch (header.type) {
			// Container sections — recurse into them.
			case RWSectionType::Extension:
			case RWSectionType::Texture:
			case RWSectionType::Material:
			case RWSectionType::MaterialList:
			case RWSectionType::FrameList:
			case RWSectionType::Geometry:
			case RWSectionType::Clump:
			case RWSectionType::Atomic:
			case RWSectionType::GeometryList:
				process_section(header);
				break;

			case RWSectionType::Struct:
				parse_struct(header, parent.type);
				break;

			case RWSectionType::BinMesh:
				parse_bin_mesh();
				break;

			case RWSectionType::Effect2D:
				parse_2dfx(header.size);
				break;

			case RWSectionType::Frame:
				parse_frame_name(header.size);
				break;

			case RWSectionType::String:
				parse_string_section(header.size, parent.type);
				break;

			default:
				// Skip unknown sections.
				break;
		}

		// Ensure we're at the correct position after processing.
		reader.set_position(section_end);
	}
}

// =============================================================================
// Struct parser — dispatches based on parent section type
// =============================================================================

void DffParserInternal::parse_struct(const RWSectionHeader &header, RWSectionType parent_type) {
	switch (parent_type) {
		case RWSectionType::FrameList:
			parse_frames_list();
			break;
		case RWSectionType::Geometry:
			parse_geometry(header);
			break;
		case RWSectionType::Atomic:
			parse_atomic();
			break;
		case RWSectionType::Material:
			parse_material();
			break;
		default:
			reader.skip(header.size);
			break;
	}
}

// =============================================================================
// Frame list — reads frame hierarchy (rotation matrix, position, parent)
// =============================================================================

void DffParserInternal::parse_frames_list() {
	int32_t frame_count = reader.read_int32();
	frames.resize(frame_count);

	for (int32_t i = 0; i < frame_count; i++) {
		ParsedFrame frame;

		// 3x3 rotation matrix (36 bytes) — skip for map objects.
		reader.skip(36);

		// Position: GTA(x, y, z) → Godot(x, z, -y)
		float gta_x = reader.read_float();
		float gta_y = reader.read_float();
		float gta_z = reader.read_float();
		frame.position = Vector3(gta_x, gta_z, -gta_y);

		// Parent frame index.
		frame.parent_index = reader.read_int32();

		// Skip 4 bytes (flags/unused).
		reader.skip(4);

		frames.set(i, frame);
	}
}

// =============================================================================
// Geometry — reads vertex positions, UVs, colors, normals, triangles
// =============================================================================

void DffParserInternal::parse_geometry(const RWSectionHeader &header) {
	ParsedGeometry geom;

	uint16_t flags = reader.read_uint16();
	reader.skip(2); // Unknown flags
	int32_t tri_count = reader.read_int32();
	int32_t vertex_count = reader.read_int32();
	reader.skip(4); // Morph target count

	// Pre-SA versions have ambient/diffuse/specular here.
	if (header.version < RWVersion::ViceCity_2) {
		reader.skip(12); // ambient, diffuse, specular floats
	}

	// Vertex colors (RGBA, 4 bytes per vertex).
	if (flags & GEO_HAS_COLORS) {
		geom.colors.resize(vertex_count);
		for (int32_t i = 0; i < vertex_count; i++) {
			PackedByteArray rgba = reader.read_bytes(4);
			if (rgba.size() == 4) {
				geom.colors.set(i, Color(rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, rgba[3] / 255.0f));
			}
		}
	}

	// UV coordinates.
	if (flags & GEO_HAS_UV2) {
		// Has two UV sets — read first, skip second.
		geom.uvs.resize(vertex_count);
		for (int32_t i = 0; i < vertex_count; i++) {
			float u = reader.read_float();
			float v = reader.read_float();
			geom.uvs.set(i, Vector2(u, v));
		}
		// Skip second UV set.
		reader.skip(vertex_count * 8);
	} else if (flags & GEO_HAS_UV) {
		geom.uvs.resize(vertex_count);
		for (int32_t i = 0; i < vertex_count; i++) {
			float u = reader.read_float();
			float v = reader.read_float();
			geom.uvs.set(i, Vector2(u, v));
		}
	}

	// Fallback triangles (pre-BinMesh format).
	geom.fallback_triangles.resize(tri_count * 3);
	for (int32_t i = 0; i < tri_count; i++) {
		int16_t v0 = reader.read_int16();
		int16_t v1 = reader.read_int16();
		reader.skip(2); // Material index
		int16_t v2 = reader.read_int16();
		// Reversed winding: CW→CCW for Godot.
		geom.fallback_triangles.set(i * 3 + 0, v2);
		geom.fallback_triangles.set(i * 3 + 1, v1);
		geom.fallback_triangles.set(i * 3 + 2, v0);
	}

	// Bounding sphere (16 bytes).
	reader.skip(16);

	// Has positions / has normals flags.
	int32_t has_positions = reader.read_int32();
	int32_t has_normals = reader.read_int32();

	// Vertex positions: GTA(x, y, z) → Godot(x, z, -y)
	if (has_positions) {
		geom.vertices.resize(vertex_count);
		for (int32_t i = 0; i < vertex_count; i++) {
			float gta_x = reader.read_float();
			float gta_y = reader.read_float();
			float gta_z = reader.read_float();
			geom.vertices.set(i, Vector3(gta_x, gta_z, -gta_y));
		}
	}

	// Normals: same coordinate conversion.
	if (has_normals || (flags & GEO_HAS_NORMALS)) {
		geom.normals.resize(vertex_count);
		for (int32_t i = 0; i < vertex_count; i++) {
			float gta_nx = reader.read_float();
			float gta_ny = reader.read_float();
			float gta_nz = reader.read_float();
			geom.normals.set(i, Vector3(gta_nx, gta_nz, -gta_ny));
		}
	}

	geometries.push_back(geom);
}

// =============================================================================
// Atomic — links a frame to a geometry
// =============================================================================

void DffParserInternal::parse_atomic() {
	int32_t frame_index = reader.read_int32();
	int32_t geometry_index = reader.read_int32();
	reader.skip(8); // Render flags

	if (frame_index >= 0 && frame_index < frames.size()) {
		frames.ptrw()[frame_index].geometry_index = geometry_index;
	}
}

// =============================================================================
// Material — reads color, texture flag, ambient/diffuse/specular
// =============================================================================

void DffParserInternal::parse_material() {
	DffMaterial mat;

	reader.skip(4); // Flags

	// Color (RGBA, 4 bytes).
	PackedByteArray color_bytes = reader.read_bytes(4);
	if (color_bytes.size() == 4) {
		mat.color = Color(
				color_bytes[0] / 255.0f,
				color_bytes[1] / 255.0f,
				color_bytes[2] / 255.0f,
				color_bytes[3] / 255.0f);
	}

	reader.skip(4); // Unused

	mat.textured = reader.read_int32() != 0;
	mat.ambient = reader.read_float();
	mat.specular = reader.read_float();
	mat.diffuse = reader.read_float();

	// Add to the last geometry's material list.
	if (!geometries.is_empty()) {
		geometries.ptrw()[geometries.size() - 1].materials.push_back(mat);
	}

	is_alpha_mask_string = false;
}

// =============================================================================
// BinMesh — optimized triangle data split by material (sub-meshes)
// =============================================================================

void DffParserInternal::parse_bin_mesh() {
	if (geometries.is_empty())
		return;
	ParsedGeometry &geom = geometries.ptrw()[geometries.size() - 1];

	int32_t is_strip = reader.read_int32();
	int32_t sub_mesh_count = reader.read_int32();
	reader.skip(4); // Total face count

	geom.has_bin_mesh = true;
	geom.sub_meshes.resize(sub_mesh_count);

	for (int32_t j = 0; j < sub_mesh_count; j++) {
		int32_t index_count = reader.read_int32();
		int32_t material_index = reader.read_int32();

		ParsedGeometry::SubMesh sm;
		sm.material_index = material_index;

		if (is_strip) {
			// Read strip indices and convert to triangle list.
			Vector<int32_t> strip;
			strip.resize(index_count);
			for (int32_t i = 0; i < index_count; i++) {
				strip.set(i, reader.read_int32());
			}

			// Convert triangle strip to triangle list.
			for (int32_t i = 0; i < index_count - 2; i++) {
				int32_t v0 = strip[i + 0];
				int32_t v1 = strip[i + 1 + (i & 1)];
				int32_t v2 = strip[i + 2 - (i & 1)];

				// Skip degenerate triangles.
				if (v0 == v1 || v1 == v2 || v2 == v0) {
					continue;
				}

				// Reversed winding: CW→CCW for Godot.
				sm.indices.push_back(v2);
				sm.indices.push_back(v1);
				sm.indices.push_back(v0);
			}
		} else {
			// Triangle list — reversed winding: CW→CCW for Godot.
			int32_t tri_count_list = index_count / 3;
			sm.indices.resize(tri_count_list * 3);
			for (int32_t i = 0; i < tri_count_list; i++) {
				int32_t i0 = reader.read_int32();
				int32_t i1 = reader.read_int32();
				int32_t i2 = reader.read_int32();
				// Reversed winding: CW→CCW for Godot.
				sm.indices.set(i * 3 + 0, i2);
				sm.indices.set(i * 3 + 1, i1);
				sm.indices.set(i * 3 + 2, i0);
			}
			// Consume any remaining indices that don't form a complete triangle
			// (some broken DFFs have trailing garbage indices).
			for (int32_t i = tri_count_list * 3; i < index_count; i++) {
				reader.read_int32();
			}
		}

		geom.sub_meshes.set(j, sm);
	}
}


void DffParserInternal::parse_2dfx(int32_t p_size) {
	if (geometries.is_empty())
		return;
	ParsedGeometry &geom = geometries.ptrw()[geometries.size() - 1];

	int64_t end = reader.get_position() + p_size;
	int32_t entry_count = reader.read_int32();

	for (int32_t i = 0; i < entry_count && reader.get_position() < end; i++) {
		float gta_x = reader.read_float();
		float gta_y = reader.read_float();
		float gta_z = reader.read_float();
		int32_t entry_type = reader.read_int32();
		int32_t data_size = reader.read_int32();
		int64_t entry_end = reader.get_position() + data_size;

		if (entry_type == 0 && (data_size == 76 || data_size == 80)) {
			Dff2dfxLight light;
			light.local_offset = Vector3(gta_x, gta_z, -gta_y);

			light.red = reader.read_uint8();
			light.green = reader.read_uint8();
			light.blue = reader.read_uint8();
			light.alpha = reader.read_uint8();
			light.corona_far_clip = reader.read_float();
			light.pointlight_range = reader.read_float();
			light.corona_size = reader.read_float();
			light.shadow_size = reader.read_float();
			light.corona_show_mode = reader.read_uint8();
			light.corona_enable_reflection = reader.read_uint8();
			light.corona_flare_type = reader.read_uint8();
			light.shadow_color_multiplier = reader.read_uint8();
			light.flags = reader.read_uint8();
			light.corona_texture_name = reader.read_string(24);
			light.shadow_texture_name = reader.read_string(24);
			light.shadow_z_distance = reader.read_uint8();
			light.flags2 = reader.read_uint8();

			geom.lights.push_back(light);
		}

		// Entradas que no son luz (partículas, ped attractors, letreros...)
		// simplemente se saltan usando su propio data_size.
		reader.set_position(entry_end);
	}
}

// =============================================================================
// Frame name — extension section giving a frame its string name
// =============================================================================

void DffParserInternal::parse_frame_name(int32_t size) {
	if (current_frame_name < frames.size()) {
		frames.ptrw()[current_frame_name].name = reader.read_string(size);
		current_frame_name++;
	} else {
		reader.skip(size);
	}
}

// =============================================================================
// String section — used inside Texture sections for texture/mask names
// =============================================================================

void DffParserInternal::parse_string_section(int32_t size, RWSectionType parent_type) {
	if (parent_type != RWSectionType::Texture || geometries.is_empty()) {
		reader.skip(size);
		return;
	}

	String str = reader.read_string(size);
	ParsedGeometry &geom = geometries.ptrw()[geometries.size() - 1];

	if (!geom.materials.is_empty()) {
		DffMaterial &mat = geom.materials.ptrw()[geom.materials.size() - 1];
		if (is_alpha_mask_string) {
			mat.mask_name = str;
		} else {
			mat.texture_name = str;
		}
	}

	is_alpha_mask_string = !is_alpha_mask_string;
}

// =============================================================================
// Builds a standalone ArrayMesh (+ per-surface materials) for exactly one
// parsed geometry entry. Factored out so both the merged "one mesh for the
// whole clump" path (props, used by MapBuilder/GTAModelInstance) and the new
// "one mesh per named part" path (vehicles, used by GTAVehicleInstance) share
// the exact same vertex/index/color assembly code.
// =============================================================================

static void build_geometry_mesh(const ParsedGeometry &geom, Ref<ArrayMesh> &r_mesh, Vector<DffMaterial> &r_materials) {
	r_mesh.instantiate();
	r_materials.clear();

	if (geom.vertices.is_empty()) {
		return;
	}

	// Convert vertex data to Godot packed arrays.
	PackedVector3Array packed_vertices;
	PackedVector3Array packed_normals;
	PackedVector2Array packed_uvs;
	PackedColorArray packed_colors;

	packed_vertices.resize(geom.vertices.size());
	for (int i = 0; i < geom.vertices.size(); i++) {
		packed_vertices.set(i, geom.vertices[i]);
	}

	packed_normals.resize(geom.vertices.size());
	if (!geom.normals.is_empty()) {
		for (int i = 0; i < geom.normals.size(); i++) {
			packed_normals.set(i, geom.normals[i]);
		}
	} else {
		packed_normals.fill(Vector3(0, 1, 0));
	}

	packed_uvs.resize(geom.vertices.size());
	if (!geom.uvs.is_empty()) {
		for (int i = 0; i < geom.uvs.size(); i++) {
			packed_uvs.set(i, geom.uvs[i]);
		}
	} else {
		packed_uvs.fill(Vector2(0, 0));
	}

	packed_colors.resize(geom.vertices.size());
	if (!geom.colors.is_empty()) {
		for (int i = 0; i < geom.colors.size(); i++) {
			packed_colors.set(i, geom.colors[i]);
		}
	} else {
		packed_colors.fill(Color(1, 1, 1, 1));
	}

	// Build surfaces from BinMesh sub-meshes (or fallback triangles).
	if (geom.has_bin_mesh && !geom.sub_meshes.is_empty()) {
		for (int s = 0; s < geom.sub_meshes.size(); s++) {
			const ParsedGeometry::SubMesh &sm = geom.sub_meshes[s];
			if (sm.indices.is_empty())
				continue;

			PackedInt32Array packed_indices;
			packed_indices.resize(sm.indices.size());
			for (int i = 0; i < sm.indices.size(); i++) {
				packed_indices.set(i, sm.indices[i]);
			}

			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = packed_vertices;
			arrays[Mesh::ARRAY_NORMAL] = packed_normals;
			arrays[Mesh::ARRAY_TEX_UV] = packed_uvs;
			arrays[Mesh::ARRAY_COLOR] = packed_colors;
			arrays[Mesh::ARRAY_INDEX] = packed_indices;

			r_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

			// Track which material this surface uses.
			if (sm.material_index >= 0 && sm.material_index < geom.materials.size()) {
				r_materials.push_back(geom.materials[sm.material_index]);
			} else {
				r_materials.push_back(DffMaterial());
			}
		}
	} else if (!geom.fallback_triangles.is_empty()) {
		// Single surface with fallback triangles.
		PackedInt32Array packed_indices;
		packed_indices.resize(geom.fallback_triangles.size());
		for (int i = 0; i < geom.fallback_triangles.size(); i++) {
			packed_indices.set(i, geom.fallback_triangles[i]);
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = packed_vertices;
		arrays[Mesh::ARRAY_NORMAL] = packed_normals;
		arrays[Mesh::ARRAY_TEX_UV] = packed_uvs;
		arrays[Mesh::ARRAY_COLOR] = packed_colors;
		arrays[Mesh::ARRAY_INDEX] = packed_indices;

		r_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

		if (!geom.materials.is_empty()) {
			r_materials.push_back(geom.materials[0]);
		} else {
			r_materials.push_back(DffMaterial());
		}
	}
}

// =============================================================================
// Public API — parse a DFF file from raw bytes
// =============================================================================

DffResult DffParser::parse(const PackedByteArray &p_data) {
	DffResult result;

	if (p_data.is_empty()) {
		return result;
	}

	DffParserInternal parser;
	parser.reader = BinaryReader(p_data);

	// Read the root section header and process recursively.
	RWSectionHeader root = parser.reader.read_section_header();
	parser.process_section(root);

	if (parser.geometries.is_empty()) {
		return result;
	}

	// Find all geometries linked by Atomics.
	Vector<int> active_geometries;
	for (int i = 0; i < parser.frames.size(); i++) {
		if (parser.frames[i].geometry_index >= 0 && parser.frames[i].geometry_index < parser.geometries.size()) {
			// Skip LOD geometries which have frames ending in "_l1".
			// Unity explicitly hides these to prevent Z-fighting.
			if (parser.frames[i].name.to_lower().ends_with("_l1")) {
				continue;
			}
			active_geometries.push_back(parser.frames[i].geometry_index);
		}
	}

	// Fallback if no Atomics link to geometries.
	if (active_geometries.is_empty()) {
		active_geometries.push_back(0);
	}

	result.mesh.instantiate();

	for (int g = 0; g < active_geometries.size(); g++) {
		const ParsedGeometry &geom = parser.geometries[active_geometries[g]];

		result.lights.append_array(geom.lights);

		if (geom.vertices.is_empty()) {
			continue;
		}

		// Build this geometry into a small standalone mesh, then fold its
		// surfaces into the merged result.mesh — output is identical to the
		// previous inline version, just routed through the shared helper.
		Ref<ArrayMesh> part_mesh;
		Vector<DffMaterial> part_materials;
		build_geometry_mesh(geom, part_mesh, part_materials);

		for (int s = 0; s < part_mesh->get_surface_count(); s++) {
			result.mesh->add_surface_from_arrays(part_mesh->surface_get_primitive_type(s), part_mesh->surface_get_arrays(s));
			result.materials.push_back(part_materials[s]);
		}
	}

	// -- Per-frame data + standalone per-geometry meshes --------------------
	// Additive: existing consumers (MapBuilder, GTAModelInstance) only ever
	// read result.mesh/result.materials above, which are unchanged. This is
	// only read by GTAVehicleInstance, which needs individual named parts
	// (wheels, doors, ...) instead of one merged blob.
	result.frames.resize(parser.frames.size());
	for (int i = 0; i < parser.frames.size(); i++) {
		DffFrame frame;
		frame.name = parser.frames[i].name;
		frame.position = parser.frames[i].position;
		frame.parent_index = parser.frames[i].parent_index;
		frame.geometry_index = parser.frames[i].geometry_index;
		result.frames.set(i, frame);
	}

	result.geometry_meshes.resize(parser.geometries.size());
	result.geometry_materials.resize(parser.geometries.size());
	for (int g = 0; g < parser.geometries.size(); g++) {
		Ref<ArrayMesh> geom_mesh;
		Vector<DffMaterial> geom_materials;
		build_geometry_mesh(parser.geometries[g], geom_mesh, geom_materials);
		result.geometry_meshes.set(g, geom_mesh);
		result.geometry_materials.set(g, geom_materials);
	}

	return result;
}

Vector3 DffParser::accumulate_frame_position(const Vector<DffFrame> &p_frames, int32_t p_index) {
	Vector3 pos;
	int32_t i = p_index;
	int guard = 0; // Defensive: bail out instead of looping forever on a malformed/cyclic parent chain.
	while (i >= 0 && i < p_frames.size() && guard++ < 64) {
		pos += p_frames[i].position;
		i = p_frames[i].parent_index;
	}
	return pos;
}
