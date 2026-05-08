#ifndef WATER_PARSER_H
#define WATER_PARSER_H

#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// WaterPlane
// =============================================================================

struct WaterPlane {
	Vector3 p1;
	Vector3 p2;
	Vector3 p3;
	Vector3 p4;
	int mode;
	bool is_triangle = false;
};

// =============================================================================
// WaterParser
// =============================================================================

class WaterParser {
public:
	static Vector<WaterPlane> parse(const String &p_path);
};

#endif // WATER_PARSER_H
