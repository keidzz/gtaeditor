#ifndef GTAEDITOR_COL_FILE_H
#define GTAEDITOR_COL_FILE_H

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

/// Parser for GTA San Andreas collision files (COL1 format).
/// Reference: https://gtamods.com/wiki/Collision_File
struct ColFile {
	// ── Inner types ──────────────────────────────────────────────────────

	/// Surface properties for collision primitives.
	struct TSurface {
		uint8_t material = 0;
		uint8_t flag = 0;
		uint8_t brightness = 0;
		uint8_t light = 0;
	};

	/// Base class tag for collision primitives stored in the collisions vector.
	enum class PrimitiveType { SPHERE, BOX };

	/// A collision primitive (sphere or box).
	struct Primitive {
		PrimitiveType type;
		TSurface surface;

		// Sphere data
		float radius = 0.0f;
		Vector3 center;

		// Box data
		Vector3 box_min;
		Vector3 box_max;
	};

	/// Bounding volume for the entire collision model.
	struct TBounds {
		float radius = 0.0f;
		Vector3 center;
		Vector3 min;
		Vector3 max;
	};

	// ── Data ─────────────────────────────────────────────────────────────

	String fourcc;
	int file_size = 0;
	String model_name;
	int model_id = 0;
	TBounds bounds;
	Vector<Primitive> collisions;
	PackedVector3Array vertices; // Triangle faces (3 vertices per face)

	// ── Methods ──────────────────────────────────────────────────────────

	/// Parse a COL1 collision file from the given file handle.
	/// The file position should be at the start of the "COLL" header.
	void parse(Ref<FileAccess> file);

private:
	/// Convert GTA coordinates (X, Y, Z) to Godot coordinates (X, Z, -Y).
	static Vector3 read_vector3(Ref<FileAccess> file);

	/// Read a 4-byte surface descriptor.
	static TSurface read_surface(Ref<FileAccess> file);
};

#endif // GTAEDITOR_COL_FILE_H
