#include "col_file.h"

using namespace godot;

// ── Helpers ──────────────────────────────────────────────────────────────────

Vector3 ColFile::read_vector3(Ref<FileAccess> file) {
	// GTA uses (X, Y, Z) where Y is forward and Z is up.
	// Godot uses (X, Y, Z) where Z is forward and Y is up.
	// Conversion: Godot.X = GTA.X, Godot.Y = GTA.Z, Godot.Z = -GTA.Y
	float x = file->get_float();
	float y = file->get_float();
	float z = file->get_float();
	return Vector3(x, z, -y);
}

ColFile::TSurface ColFile::read_surface(Ref<FileAccess> file) {
	TSurface s;
	s.material   = file->get_8();
	s.flag       = file->get_8();
	s.brightness = file->get_8();
	s.light      = file->get_8();
	return s;
}

// ── Main parser ──────────────────────────────────────────────────────────────

void ColFile::parse(Ref<FileAccess> file) {
	// Read header
	fourcc = file->get_buffer(4).get_string_from_ascii();
	ERR_FAIL_COND_MSG(fourcc != "COLL", "Invalid collision file: expected COLL header.");

	file_size  = file->get_32();
	model_name = file->get_buffer(22).get_string_from_ascii();
	model_id   = file->get_16();

	// Bounding volume
	bounds.radius = file->get_float();
	bounds.center = read_vector3(file);
	bounds.min    = read_vector3(file);
	bounds.max    = read_vector3(file);

	// ── Spheres ──────────────────────────────────────────────────────────
	uint32_t sphere_count = file->get_32();
	for (uint32_t i = 0; i < sphere_count; i++) {
		Primitive p;
		p.type    = PrimitiveType::SPHERE;
		p.radius  = file->get_float();
		p.center  = read_vector3(file);
		p.surface = read_surface(file);
		collisions.push_back(p);
	}

	// Skip unknown int32
	file->get_32();

	// ── Boxes ────────────────────────────────────────────────────────────
	uint32_t box_count = file->get_32();
	for (uint32_t i = 0; i < box_count; i++) {
		Primitive p;
		p.type    = PrimitiveType::BOX;
		p.box_min = read_vector3(file);
		p.box_max = read_vector3(file);
		p.surface = read_surface(file);
		collisions.push_back(p);
	}

	// ── Vertices ─────────────────────────────────────────────────────────
	uint32_t vert_count = file->get_32();
	Vector<Vector3> unsorted;
	unsorted.resize(vert_count);
	for (uint32_t i = 0; i < vert_count; i++) {
		unsorted.write[i] = read_vector3(file);
	}

	// ── Faces ────────────────────────────────────────────────────────────
	// Each face indexes into the unsorted vertex array.
	// We flatten into a triangle list for ConcavePolygonShape3D.
	uint32_t face_count = file->get_32();
	vertices.resize(face_count * 3);

	for (uint32_t i = 0; i < face_count; i++) {
		uint32_t a = file->get_32();
		uint32_t b = file->get_32();
		uint32_t c = file->get_32();
		read_surface(file); // Consume surface bytes (not used for shape)

		vertices.set(i * 3 + 0, unsorted[a]);
		vertices.set(i * 3 + 1, unsorted[b]);
		vertices.set(i * 3 + 2, unsorted[c]);
	}
}
