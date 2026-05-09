#include "col_parser.h"

#include "../rw/binary_reader.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// Helper to convert GTA coordinate system to Godot
static Vector3 gta_to_godot(const Vector3 &p_vec) {
	return Vector3(p_vec.x, p_vec.z, -p_vec.y);
}

// Helper to safely read a Vector3 (C++ argument evaluation order is unspecified)
static Vector3 read_vector3(BinaryReader &reader) {
	float x = reader.read_float();
	float y = reader.read_float();
	float z = reader.read_float();
	return Vector3(x, y, z);
}

HashMap<String, ColModel> ColParser::parse(const String &p_absolute_path) {
	Ref<FileAccess> file = FileAccess::open(p_absolute_path, FileAccess::READ);
	if (file.is_null()) {
		UtilityFunctions::printerr("[ColParser] Failed to open: ", p_absolute_path);
		return HashMap<String, ColModel>();
	}

	PackedByteArray bytes = file->get_buffer(file->get_length());
	return parse_bytes(bytes, p_absolute_path);
}

HashMap<String, ColModel> ColParser::parse_bytes(const PackedByteArray &p_bytes, const String &p_debug_name) {
	HashMap<String, ColModel> models;
	if (p_bytes.is_empty()) {
		return models;
	}

	BinaryReader reader(p_bytes);

	while (reader.get_position() < reader.get_length()) {
		int64_t start_pos = reader.get_position();

		if (!reader.can_read(8)) {
			break;
		}

		String fourcc = reader.read_string(4);
		uint32_t size = reader.read_uint32();

		if (fourcc != "COLL" && fourcc != "COL2" && fourcc != "COL3") {
			// Skip unknown
			reader.set_position(start_pos + 8 + size);
			continue;
		}

		String name = reader.read_string(22);
		name = name.to_lower();
		int16_t model_id = reader.read_int16();

		ColModel model;
		model.name = name;
		model.model_id = model_id;

		// Read TBounds
		Vector3 b_min, b_max, b_center;
		float b_radius;

		if (fourcc == "COLL") {
			b_radius = reader.read_float();
			b_center = read_vector3(reader);
			b_min = read_vector3(reader);
			b_max = read_vector3(reader);
		} else { // COL2 / COL3
			b_min = read_vector3(reader);
			b_max = read_vector3(reader);
			b_center = read_vector3(reader);
			b_radius = reader.read_float();
		}

		if (fourcc == "COL2" || fourcc == "COL3") {
			uint16_t num_spheres = reader.read_uint16();
			uint16_t num_boxes = reader.read_uint16();
			uint16_t num_faces = reader.read_uint16();
			uint8_t num_lines = reader.read_string(1)[0];
			reader.skip(1); // padding
			uint32_t flags = reader.read_uint32();

			uint32_t off_spheres = reader.read_uint32();
			uint32_t off_boxes = reader.read_uint32();
			uint32_t off_lines = reader.read_uint32();
			uint32_t off_vertices = reader.read_uint32();
			uint32_t off_faces = reader.read_uint32();
			uint32_t off_planes = reader.read_uint32();

			int64_t base_offset = start_pos + 4; // Offsets are relative to after FourCC

			// Spheres
			if (num_spheres > 0 && off_spheres > 0) {
				reader.set_position(base_offset + off_spheres);
				for (int i = 0; i < num_spheres; i++) {
					Vector3 center = read_vector3(reader);
					float radius = reader.read_float();
					reader.skip(4); // TSurface

					Ref<SphereShape3D> sphere;
					sphere.instantiate();
					sphere->set_radius(radius);

					ColShapeData shape_data;
					shape_data.shape = sphere;
					shape_data.transform.origin = gta_to_godot(center);
					model.shapes.push_back(shape_data);
				}
			}

			// Boxes
			if (num_boxes > 0 && off_boxes > 0) {
				reader.set_position(base_offset + off_boxes);
				for (int i = 0; i < num_boxes; i++) {
					Vector3 min_b = read_vector3(reader);
					Vector3 max_b = read_vector3(reader);
					reader.skip(4); // TSurface

					Vector3 center = (max_b + min_b) / 2.0f;
					Vector3 extents = (max_b - min_b);

					Ref<BoxShape3D> box;
					box.instantiate();
					// Godot box size is the full dimension
					box->set_size(Vector3(extents.x, extents.z, extents.y));

					ColShapeData shape_data;
					shape_data.shape = box;
					shape_data.transform.origin = gta_to_godot(center);
					model.shapes.push_back(shape_data);
				}
			}

			// Faces (Trimesh)
			if (num_faces > 0 && off_faces > 0 && off_vertices > 0) {
				PackedVector3Array godot_faces;
				godot_faces.resize(num_faces * 3);

				reader.set_position(base_offset + off_faces);
				for (int i = 0; i < num_faces; i++) {
					uint16_t a = reader.read_uint16();
					uint16_t b = reader.read_uint16();
					uint16_t c = reader.read_uint16();
					reader.skip(2); // material, light

					// Save current position to continue reading faces
					int64_t face_pos = reader.get_position();

					auto read_vertex = [&](uint16_t idx) -> Vector3 {
						reader.set_position(base_offset + off_vertices + idx * 6);
						float vx = reader.read_int16() / 128.0f;
						float vy = reader.read_int16() / 128.0f;
						float vz = reader.read_int16() / 128.0f;
						return gta_to_godot(Vector3(vx, vy, vz));
					};

					Vector3 va = read_vertex(a);
					Vector3 vb = read_vertex(b);
					Vector3 vc = read_vertex(c);

					// Godot needs CCW winding. Since we mapped coordinates with det=+1,
					// GTA's CW winding is still CW in Godot. So we reverse to (va, vc, vb) or (vc, vb, va).
					godot_faces.set(i * 3 + 0, vc);
					godot_faces.set(i * 3 + 1, vb);
					godot_faces.set(i * 3 + 2, va);

					// Restore position
					reader.set_position(face_pos);
				}

				Ref<ConcavePolygonShape3D> trimesh;
				trimesh.instantiate();
				trimesh->set_faces(godot_faces);
				trimesh->set_backface_collision_enabled(true);

				ColShapeData shape_data;
				shape_data.shape = trimesh;
				// Transform is identity, vertices are absolute
				model.shapes.push_back(shape_data);
			}
		} else if (fourcc == "COLL") {
			uint32_t num_spheres = reader.read_uint32();
			if (num_spheres > 0) {
				for (int i = 0; i < num_spheres; i++) {
					float radius = reader.read_float();
					Vector3 center = read_vector3(reader);
					reader.skip(4); // TSurface

					Ref<SphereShape3D> sphere;
					sphere.instantiate();
					sphere->set_radius(radius);

					ColShapeData shape_data;
					shape_data.shape = sphere;
					shape_data.transform.origin = gta_to_godot(center);
					model.shapes.push_back(shape_data);
				}
			}

			reader.skip(4); // unk (0)

			uint32_t num_boxes = reader.read_uint32();
			if (num_boxes > 0) {
				for (int i = 0; i < num_boxes; i++) {
					Vector3 min_b = read_vector3(reader);
					Vector3 max_b = read_vector3(reader);
					reader.skip(4); // TSurface

					Vector3 center = (max_b + min_b) / 2.0f;
					Vector3 extents = (max_b - min_b);

					Ref<BoxShape3D> box;
					box.instantiate();
					box->set_size(Vector3(extents.x, extents.z, extents.y));

					ColShapeData shape_data;
					shape_data.shape = box;
					shape_data.transform.origin = gta_to_godot(center);
					model.shapes.push_back(shape_data);
				}
			}

			uint32_t num_vertices = reader.read_uint32();
			int64_t off_vertices = reader.get_position();
			reader.skip(num_vertices * 12); // Vertices are float[3]

			uint32_t num_faces = reader.read_uint32();
			if (num_faces > 0 && num_vertices > 0) {
				PackedVector3Array godot_faces;
				godot_faces.resize(num_faces * 3);

				for (int i = 0; i < num_faces; i++) {
					uint32_t a = reader.read_uint32();
					uint32_t b = reader.read_uint32();
					uint32_t c = reader.read_uint32();
					reader.skip(4); // surface

					int64_t face_pos = reader.get_position();

					auto read_vertex = [&](uint32_t idx) -> Vector3 {
						reader.set_position(off_vertices + idx * 12);
						float vx = reader.read_float();
						float vy = reader.read_float();
						float vz = reader.read_float();
						return gta_to_godot(Vector3(vx, vy, vz));
					};

					Vector3 va = read_vertex(a);
					Vector3 vb = read_vertex(b);
					Vector3 vc = read_vertex(c);

					godot_faces.set(i * 3 + 0, vc);
					godot_faces.set(i * 3 + 1, vb);
					godot_faces.set(i * 3 + 2, va);

					reader.set_position(face_pos);
				}

				Ref<ConcavePolygonShape3D> trimesh;
				trimesh.instantiate();
				trimesh->set_faces(godot_faces);
				trimesh->set_backface_collision_enabled(true);

				ColShapeData shape_data;
				shape_data.shape = trimesh;
				model.shapes.push_back(shape_data);
			}
		}

		models[model.name] = model;

		// Move to next model
		reader.set_position(start_pos + 8 + size);
	}

	return models;
}
