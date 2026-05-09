#pragma once

#include <godot_cpp/classes/shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/templates/hash_map.hpp>

using namespace godot;

struct ColShapeData {
	Ref<Shape3D> shape;
	Transform3D transform; // Local transform for primitives (spheres, boxes) relative to the node origin.
};

struct ColModel {
	String name;
	int16_t model_id;
	Vector<ColShapeData> shapes;
};

class ColParser {
public:
	// Parses a full .col file which may contain multiple collision models.
	// Returns a map of model_name (lowercase) to ColModel.
	static HashMap<String, ColModel> parse(const String &p_absolute_path);
	static HashMap<String, ColModel> parse_bytes(const PackedByteArray &p_bytes, const String &p_debug_name = "memory");
};
