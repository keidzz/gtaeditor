/*
    GTADffGeometry — parses a .dff's mesh data: GEOMETRYLIST -> the first
    GEOMETRY chunk's vertices, normals, UVs, and triangles, and builds a
    Godot ArrayMesh from it.

    Same situation as GTADffSkeleton: NOT ported from gta-reversed (its RW
    stream reading thunks into the original exe, nothing to port), written
    against librw (github.com/aap/librw) as the public-format reference.
    Provenance:
      - Chunk type IDs (ID_GEOMETRYLIST=0x1A, ID_GEOMETRY=0x0F,
        ID_MATLIST=0x08, ID_ATOMIC=0x14): src/rwbase.h
      - GEOMETRYLIST structure (STRUCT{numGeometries}, then numGeometries *
        GEOMETRY chunks): src/clump.cpp, Clump::streamRead
      - GEOMETRY byte layout, including the format-flags-driven optional
        sections and the packed triangle format: src/geometry.cpp,
        Geometry::streamRead / geoStructSize / Geometry::create
          (numTexCoordSets derivation: (flags>>16)&0xFF, or 1 if TEXTURED,
           2 if TEXTURED2, else 0 — Geometry::create, ~line 38)

    GEOMETRY chunk layout (inside its STRUCT sub-chunk), version >= 0x34000
    (GTA:SA always is, so the pre-0x34000 SurfaceProperties block is never
    read here):
        uint32 flags; int32 numTriangles; int32 numVertices; int32 numMorphTargets;
        if flags & PRELIT:        numVertices * RGBA8 (per-vertex colors)
        numTexCoordSets * (numVertices * 2 floats)  (UV sets)
        numTriangles * { uint32 v0v1_packed; uint32 matId_v2_packed }
            -- v0 = tribuf[0]>>16, v1 = tribuf[0]&0xFFFF, v2 = tribuf[1]>>16, matId = tribuf[1]&0xFFFF
        per morph target (GTA:SA meshes always have exactly 1, no morphing):
            boundingSphere (4 floats); int32 hasVertices; int32 hasNormals;
            if hasVertices: numVertices * 3 floats (positions)
            if hasNormals:  numVertices * 3 floats (normals)
        then MATLIST chunk, then an EXTENSION (skin plugin lives here — not
        parsed by this class yet).

    SCOPE: vertices/normals/UVs/triangles/skin-weights, from the FIRST
    geometry in the file. NOT done yet, deliberately:
      - Materials/textures — renders with Godot's default material.
      - Multiple geometries / Atomic-to-frame binding — if a .dff has more
        than one geometry (get_geometry_count() > 1), only the first is used.
      - NATIVE-flag geometry (platform-specific vertex data format) is
        detected and rejected with a clear error rather than mis-parsed —
        shouldn't occur for GTA:SA PC assets, but checked defensively.

    Coordinate conversion: measured directly (see convert_z_up_to_y_up in the
    .cpp), NOT assumed. Raw player part vertex data is stored as (X=forward,
    Y=right, Z=up). In skeleton T-space (right +X, forward +Y, -up +Z),
    positions and normals both map via v_engine = T(v_raw) = (v.y, v.x, -v.z).

    Triangle winding: CONFIRMED needs flipping (build_array_mesh() now
    defaults flip_winding=true). A real torso.dff rendered hollow/see-through
    with the flip off — Godot's back-face culling is driven by each
    triangle's screen-space vertex order, not its normals, so that symptom
    is the unambiguous signature of reversed winding. My earlier guess (that
    RW/DirectX-lineage engines share Godot's clockwise convention, so no
    flip would be needed) was wrong — worth remembering as a reminder that
    "same lineage" doesn't guarantee "same convention" any more than shared
    Z-up ancestry guaranteed the skeleton and geometry data would agree with
    each other.
*/
#ifndef GTA_DFF_GEOMETRY_H
#define GTA_DFF_GEOMETRY_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class GTADffGeometry : public RefCounted {
	GDCLASS(GTADffGeometry, RefCounted)

private:
	std::vector<Vector3> vertices;
	std::vector<Vector3> normals; // empty if the source had no NORMALS flag
	std::vector<Vector2> uvs;     // empty if the source had no texture coordinate sets
	std::vector<int32_t> triangle_indices; // 3 per triangle
	std::vector<int32_t> triangle_material_ids; // 1 per triangle, kept for when materials are wired in later
	int32_t geometry_count_in_file = 0;

	// --- Skin weights (ID_SKIN extension on the GEOMETRY chunk) ---
	// [SOURCED] librw src/skin.cpp, readSkin / src/rwplugins.h, struct Skin.
	// See the .cpp for the full provenance and the open architectural
	// question this doesn't resolve on its own: whether skin_bone_indices
	// below refer directly to GTADffSkeleton's bone order, or to a smaller
	// per-geometry-file bone list that needs matching by HAnim tag/name —
	// get_skin_bone_count() is exposed specifically so that can be checked
	// empirically before assuming either way.
	bool has_skin_data = false;
	int32_t skin_bone_count = 0;
	std::vector<int32_t> skin_bone_indices; // 4 per vertex
	std::vector<float> skin_bone_weights;   // 4 per vertex, same order as indices above
	std::vector<uint8_t> skin_used_bones;
	// Baked inverse-bind matrices (numBones * 12 floats): each bone stores
	// right/up/at/pos 3-float triplets, RAW RenderWare space (no Y-up
	// conversion). [SOURCED] librw src/skin.cpp readSkin: 16 floats per bone
	// (the 4th element of each 4-float group is the RwMatrix flags/pad).
	// Exposed for validation: Godot skins with pose * inverse(rest), the game
	// skins with these baked matrices -- they must agree or the mesh deforms.
	std::vector<float> skin_inv_matrices;

protected:
	static void _bind_methods();

public:
	GTADffGeometry();
	~GTADffGeometry() override;

	bool parse(const PackedByteArray &bytes);

	int get_geometry_count() const; // total GEOMETRY chunks found in the file's GEOMETRYLIST (only the first is actually parsed)
	int get_vertex_count() const;
	int get_triangle_count() const;
	bool has_normals() const;
	bool has_uvs() const;

	// --- Skin weights ---
	bool has_skin() const;
	int get_skin_bone_count() const; // the open question: does this match GTADffSkeleton's 33, or is it a smaller per-piece subset?

  PackedInt32Array get_skin_used_bones() const;
	// Array of 4 Dictionaries {"bone": int, "weight": float} per vertex, for inspection from GDScript.
	Array get_vertex_skin_weights(int vertex_index) const;
	// RAW RenderWare-space inverse-bind matrix for skin bone i (12 floats:
	// right/up/at/pos triplets), or empty PackedFloat32Array if no skin data.
	PackedFloat32Array get_skin_inverse_bind_matrix(int bone_index) const;

	// [Empirically confirmed] Default flipped to true: a real torso.dff
	// rendered "hollow"/see-through with flip_winding=false, the classic
	// signature of reversed front-face winding (Godot's default back-face
	// culling is driven by each triangle's screen-space vertex order, not
	// its normals) — so RW's packed triangle format produces the opposite
	// winding from what Godot expects. Unlike the coordinate-axis question,
	// this symptom has essentially one cause, so no separate measurement
	// was needed to confirm it the way the axis conversions required.
	Ref<ArrayMesh> build_array_mesh(bool flip_winding = true) const;
};

} // namespace godot

#endif // GTA_DFF_GEOMETRY_H
