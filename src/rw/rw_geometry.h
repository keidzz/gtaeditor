#ifndef GTAEDITOR_RW_GEOMETRY_H
#define GTAEDITOR_RW_GEOMETRY_H

#include "rw_chunk.h"
#include "rw_material_list.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/templates/vector.hpp>

/// Geometry format flags (from the RenderWare specification).
enum RWGeometryFlags : uint32_t {
	rpGEOMETRY_TRISTRIP       = 0x00000001,
	rpGEOMETRY_POSITIONS      = 0x00000002,
	rpGEOMETRY_TEXTURED       = 0x00000004,
	rpGEOMETRY_PRELIT         = 0x00000008,
	rpGEOMETRY_NORMALS        = 0x00000010,
	rpGEOMETRY_LIGHT          = 0x00000020,
	rpGEOMETRY_MODULATE_COLOR = 0x00000040,
	rpGEOMETRY_TEXTURED2      = 0x00000080,
	rpGEOMETRY_NATIVE         = 0x01000000,
};

/// A single triangle referencing three vertices and a material.
struct RWTriangle {
	uint16_t vertex_1 = 0;
	uint16_t vertex_2 = 0;
	uint16_t vertex_3 = 0;
	uint16_t material_id = 0;
};

/// Bounding sphere for a morph target.
struct RWSphere {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float radius = 0.0f;
};

/// A morph target contains vertex positions and optional normals.
struct RWMorphTarget {
	RWSphere bounding_sphere;
	bool has_vertices = false;
	bool has_normals = false;
	Vector<Vector3> vertices;
	Vector<Vector3> normals;
};

/// Parses an RW GEOMETRY chunk and builds a Godot ArrayMesh.
/// Contains vertices, UVs, triangles, morph targets, and a material list.
class RWGeometry : public RWChunk {
public:
	uint32_t format = 0;
	uint32_t tri_count = 0;
	uint32_t vert_count = 0;
	uint32_t morph_target_count = 0;

	// Only present if version < 0x34000
	float ambient = 0.0f;
	float specular_val = 0.0f;
	float diffuse = 0.0f;

	uint32_t uv_count = 0;
	Vector<PackedVector2Array> uvs;
	PackedColorArray prelit_colors;
	Vector<RWTriangle> tris;
	Vector<RWMorphTarget> morph_targets;
	RWMaterialList material_list;

	/// Parse a GEOMETRY chunk from the current file position.
	void parse(Ref<FileAccess> file);

	/// Build and return an ArrayMesh from the parsed geometry data.
	/// Groups triangles by material for efficient rendering.
	Ref<ArrayMesh> build_mesh();
};

#endif // GTAEDITOR_RW_GEOMETRY_H
