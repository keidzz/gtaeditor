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
// DFF Frame — one node in the clump's frame hierarchy (chassis_dummy,
// wheel_lf_dummy, door_lf_dummy, ...). Exposed so callers that need
// individual named parts (vehicles) can position/filter them themselves,
// instead of only getting the single merged mesh used for map props.
// =============================================================================

struct DffFrame {
	String name;
	Vector3 position; // GTA→Godot converted, relative to parent_index (translation only, see accumulate_frame_position()).
	int32_t parent_index = -1;
	int32_t geometry_index = -1; // -1 if this frame has no atomic attached.
};

struct Dff2dfxLight {
	Vector3 local_offset;
	uint8_t red = 255;
	uint8_t green = 255;
	uint8_t blue = 255;
	uint8_t alpha = 255;
	float corona_far_clip = 0.0f;
	float pointlight_range = 0.0f;
	float corona_size = 0.0f;
	float shadow_size = 0.0f;
	uint8_t corona_show_mode = 0;
	uint8_t corona_enable_reflection = 0;
	uint8_t corona_flare_type = 0;
	uint8_t shadow_color_multiplier = 0;
	uint8_t flags = 0;
	String corona_texture_name;
	String shadow_texture_name;
	uint8_t shadow_z_distance = 0;
	uint8_t flags2 = 0;
};

// =============================================================================
// DFF Parse Result — contains the mesh and per-surface material info.
// =============================================================================

struct DffResult {
	Ref<ArrayMesh> mesh;
	Ref<ConcavePolygonShape3D> col_shape;
	Vector<DffMaterial> materials; // One per mesh surface
	Vector<Dff2dfxLight> lights;

	// -- Added for GTAVehicleInstance (and any future multi-part consumer) --
	// The merged `mesh` above stays byte-for-byte identical to before; these
	// are purely additive and unused by MapBuilder/GTAModelInstance.
	Vector<DffFrame> frames; // Every frame in the clump, in file order.
	Vector<Ref<ArrayMesh>> geometry_meshes; // One mesh per raw geometry entry, indexed the same way frames[i].geometry_index is.
	Vector<Vector<DffMaterial>> geometry_materials; // Parallel to geometry_meshes; one DffMaterial per surface.
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

	// Sums local positions from p_index up through its parent chain, giving
	// that frame's position relative to the clump root. Translation-only —
	// does not account for a parent frame's rotation matrix (currently
	// discarded during parsing; see the "skip(36)" comment in
	// parse_frames_list() in dff_parser.cpp). Fine for standard SA vehicle
	// wheel/panel dummies, which are conventionally unrotated relative to
	// chassis_dummy.
	static Vector3 accumulate_frame_position(const Vector<DffFrame> &p_frames, int32_t p_index);
};

#endif // DFF_PARSER_H
