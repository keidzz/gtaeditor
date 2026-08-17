/*
	GTADffSkeleton — parses a .dff's bone hierarchy: the RenderWare
	FrameList chunk plus each frame's HAnim bone-ID extension.

	UNLIKE the IMG archive reader and IFP animation parser, this is NOT
	ported from gta-reversed — as established earlier, gta-reversed's own
	RenderWare stream reading (CFileLoader::LoadClumpFile and friends) thunks
	directly into the original compiled exe at hardcoded addresses; none of
	it is actually reimplemented, so there's nothing to port for the DFF
	format itself.

	Instead, this is written against the RenderWare stream format as
	documented by librw (github.com/aap/librw), a mature, actively-maintained
	open-source reimplementation of the RenderWare engine used throughout the
	GTA III/VC/SA modding community specifically for reading/writing these
	file formats. Specific provenance per piece:

	  - Generic 12-byte RW chunk header {type, size, libraryID} and the
		find-or-skip chunk search pattern: src/base.cpp, readChunkHeaderInfo/findChunk
	  - Chunk type IDs (ID_STRUCT=0x01, ID_EXTENSION=0x03, ID_FRAMELIST=0x0E,
		ID_CLUMP=0x10, ID_HANIM=0x11E): src/rwbase.h
	  - Clump top-level ordering (STRUCT, FRAMELIST, GEOMETRYLIST, ATOMICs —
		this class stops after FRAMELIST): src/clump.cpp, Clump::streamRead
	  - Per-frame binary layout, FrameStreamData {V3d right,up,at,pos; int32
		parent; int32 matflag} = 56 bytes: src/frame.cpp, FrameList_::streamRead
	  - HAnim extension payload {int32 version(==0x100); int32 id; int32
		numNodes; [if numNodes!=0: int32 flags; int32 maxKeySize; numNodes *
		{int32 nodeID; int32 index(unused); int32 nodeFlags}]}:
		src/hanim.cpp, readHAnim

	The HAnim node IDs read here are the SAME eBoneTag values gta-reversed
	uses (source/game_sa/Enums/eBoneTag.h) — that's not a coincidence, it's
	what lets an IFP animation sequence (tagged by the same IDs) know which
	skeleton bone it drives. See gta_bone_tags.h.

	SCOPE: this class stops right after the frame list. It does NOT parse
	geometry, materials, skin weights, or textures — that's a separate,
	larger piece of work, deliberately not attempted in the same pass as this.

	Coordinate conversion: the raw frame hierarchy from player.dff is Z-up
	(measured by composing the real frame matrices: head at global +Z ~0.59,
	feet at -Z ~-0.92, arms along Y — the same convention the geometry and
	IFP animation data use). An EARLIER version of this class concluded the
	skeleton was "already Y-up" from eyeballing local bone-origin ranges and
	made _convert_z_up_to_y_up() an identity — that was wrong, and because
	pose==rest renders identically to authored geometry, it stayed invisible
	until an animation was actually played (see the .cpp). The conversion is
	now real and is applied in the CONJUGATE form (T*local*T^-1) so the
	recomposed hierarchy is an exact rigid transform of the original — see
	the .cpp comment for the numerical verification and why plain
	left-multiplication would twist the hierarchy.
*/
#ifndef GTA_DFF_SKELETON_H
#define GTA_DFF_SKELETON_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class GTADffSkeleton : public RefCounted {
	GDCLASS(GTADffSkeleton, RefCounted)

public:
	struct Bone {
		String name; // resolved from hanim_id via gta_bone_tag_to_name(), or "Frame_N" if no HAnim extension was found
		int32_t hanim_id = -1; // -1 if this frame has no HAnim extension at all
		int32_t parent_index = -1; // index into `bones`, -1 for the root
		Transform3D bind_transform; // RAW RenderWare local transform (right,up,at columns) + pos — Z-up on disk; converted to Y-up at build time via _convert_z_up_to_y_up (see the .cpp note on why the conjugate form is required, not a plain T*local)
	};

	struct HierarchyNode {
		int32_t hanim_id = -1;
		// CORRECTED, [SOURCED] librw src/hanim.cpp readHAnim(): this field is
		// the per-node on-disk "index" value from the HAnim extension, and
		// librw's own reference reader reads it and explicitly discards it
		// ("stream->readI32(); // index...unused") — HAnimHierarchy::create()
		// sets nodeInfo[i].index to the LOOP POSITION i, never this value.
		// So it is NOT a reliable bone/skin slot number; a caller that needs
		// "which hierarchy position is this node" should use this entry's
		// ARRAY INDEX within get_hierarchy_table()'s returned Array instead
		// (matches librw's nodeInfo[i] exactly, since hierarchy_table is
		// populated in the same file/loop order). Kept here — not deleted —
		// as raw parsed data in case it's ever independently useful, but
		// gta_character_visual.gd no longer uses it for bone resolution;
		// see that file's _attach_part() for the full corrected chain
		// (also needs GTADffGeometry::get_skin_used_bones()'s remap, a
		// separate, additional step — this field alone was never sufficient
		// even before the "unused" finding).
		int32_t skin_index = -1;
		int32_t flags = 0;
	};

private:
	std::vector<Bone> bones;
  std::vector<HierarchyNode> hierarchy_table;
	int32_t root_index = -1;

	// Historically a Z-up -> Y-up conversion; empirically disproven for this
	// skeleton data and now an identity pass-through. Kept as a real
	// function (not deleted) so the reasoning trail — and the "verify
	// empirically, don't assume" lesson — stays attached to the code. See
	// the .cpp for the measurement that disproved the original assumption.
	static Transform3D _convert_z_up_to_y_up(const Transform3D &t);

protected:
	static void _bind_methods();

public:
	GTADffSkeleton();
	~GTADffSkeleton() override;

	// Parses the first FRAMELIST found in a .dff's byte content (i.e. stops
	// before geometry). Returns false with a pushed error on any format
	// mismatch or truncation.
	bool parse(const PackedByteArray &bytes);

	int get_bone_count() const;
	int get_root_index() const;
  Array get_hierarchy_table() const;

	// Array of Dictionaries: {"name", "hanim_id", "parent_index", "bind_transform"}
	// bind_transform here is always RAW/unconverted, matching what parse() read.
	Array get_bones() const;

	// -1 if not found (case-insensitive match against the resolved name)
	int find_bone_by_name(const String &name) const;

	// Computes each bone's GLOBAL bind transform by walking the parent chain
	// using ONLY our own parsed data (parent_index + bind_transform) —
	// deliberately independent of Skeleton3D's own global-pose cache, which
	// has a known, currently-open Godot engine bug where it doesn't reliably
	// update for skeletons built and rested purely from code (see the .cpp
	// comment on build_skeleton3d/this method for the tracking issue). Safe
	// to use for visualization/debugging regardless of that bug's status in
	// whatever Godot version this runs on. Array of Transform3D, one per
	// bone, in the same index order as get_bones().
	Array get_global_bind_transforms(bool convert_coordinates = false) const;

	// Builds a ready-to-use Skeleton3D (caller owns it — add_child() it
	// somewhere). The raw DFF frame hierarchy is already Y-up (Root frame
	// contains the Z-to-Y basis mapping), so convert_coordinates defaults to false.
	Skeleton3D *build_skeleton3d(bool convert_coordinates = false) const;
};

} // namespace godot

#endif // GTA_DFF_SKELETON_H
