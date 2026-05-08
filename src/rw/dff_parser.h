#ifndef DFF_PARSER_H
#define DFF_PARSER_H

#include "binary_reader.h"
#include "rw_types.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/templates/vector.hpp>

using namespace godot;

// =============================================================================
// DFF Material info — extracted during DFF parsing, used later to apply textures.
// =============================================================================

struct DffMaterial {
	String texture_name;
	String mask_name;
	Color color = Color(1, 1, 1, 1);
	float ambient = 1.0f;
	float diffuse = 1.0f;
	float specular = 0.0f;
	bool textured = false;
};

// =============================================================================
// DFF Parse Result — contains the mesh and per-surface material info.
// =============================================================================

struct DffResult {
	Ref<ArrayMesh> mesh;
	Ref<ConcavePolygonShape3D> col_shape;
	Vector<DffMaterial> materials; // One per mesh surface
};

// =============================================================================
// DffParser — Parses RenderWare DFF (model) files into Godot ArrayMesh.
//
// DFF files contain a hierarchy of RenderWare sections:
//   Clump → FrameList, GeometryList, Atomic(s)
//   GeometryList → Geometry(s)
//   Geometry → Struct (vertex/tri data), MaterialList, Extension (BinMesh)
//
// For map objects, we flatten the hierarchy and produce a single ArrayMesh
// with one surface per BinMesh sub-mesh (material split).
//
// Coordinate conversion applied to vertices: Godot(x,z,-y) = GTA(x,y,z)
// Triangle winding: reversed (CW→CCW) for Godot front-face convention
// UV: no flip needed (both GTA and Godot use top-left origin)
// =============================================================================

class DffParser {
public:
	// Parse a DFF file from raw bytes. Returns mesh + material info.
	static DffResult parse(const PackedByteArray &p_data);
};

#endif // DFF_PARSER_H
