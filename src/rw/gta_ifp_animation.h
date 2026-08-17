/*
    GTAIfpAnimation — parses GTA:SA's .ifp animation format, "ANP2"/"ANP3"
    variant only (see note on ANPK below).

    SOURCED, byte-exact port of:
      - source/game_sa/Animation/AnimManager.cpp
            CAnimManager::LoadAnimFile_ANP23 (inline, called from LoadAnimFile @ 0x4D47F0)
      - source/game_sa/Animation/AnimSequenceFrames.h
            KeyFrame (0x14), KeyFrameTrans (0x20),
            KeyFrameCompressed (0xA), KeyFrameTransCompressed (0x10)
      - source/game_sa/Core/Quaternion.h   — CQuaternion field order: x, y, z, w
      - source/game_sa/Core/Vector.h       — CVector field order: x, y, z
      - source/extensions/FixedFloat.hpp   — dequantize: float(raw) / CompressValue
      - source/extensions/FixedQuat.hpp, FixedVector.hpp
      - source/game_sa/Enums/eBoneTag.h    — bone tag -> conventional name table

    Confirmed (Animation/AnimManager.cpp:571-573,
    CAnimManager::LoadAnimFiles) that GTA:SA's own generic ped animations
    load from a LOOSE file, "ANIM\PED.IFP" (i.e. <install>/anim/ped.ifp),
    NOT from an .img archive. Mission-specific/vehicle animations may still
    be packed and need GTAImgArchive::extract_file() first — this class just
    needs the raw bytes either way.

    On why only ANP2/ANP3 is implemented here, not the older ANPK/RW-chunk
    format: gta-reversed's own LoadAnimFile_ANPK is explicitly commented
    "I didn't manage to find any ANPK anims, thus this function is untested
    so far" (AnimManager.cpp, ~line 665). Rather than port unverified logic
    and call it sourced, ANPK support is left unimplemented — parse() returns
    false with a clear error if it sees that header. In practice GTA:SA's own
    ped.ifp is expected to be ANP3 (the comment at AnimManager.cpp:858-859,
    "In ANP3 a big chunk of memory is allocated for all frames instead of
    allocating lots of small chunks", strongly implies ANP3 is SA's native
    format; ANP2 is handled by the same code path for older/intermediate
    files), so this should cover the file that actually matters here.

    IMPORTANT semantic note [CORRECTED after a real playtest surfaced it was
    wrong, then re-derived from source -- see AnimBlendHierarchy.h
    ICalcTotalTime]: the on-disk DeltaTime field is CUMULATIVE time from the
    start of that bone's own sequence, NOT a step-to-step delta as this class
    originally (incorrectly) assumed and documented here. Proof: the real
    engine's own post-load fixup computes step deltas by subtracting
    ADJACENT raw on-disk values, in place, which only makes sense if the
    on-disk values are cumulative beforehand. This class now exposes `time`
    as that raw cumulative value directly (SOURCED) and computes `delta_time`
    as the derived step-to-step value via the same backward-subtraction the
    real engine does (also SOURCED, not "own convenience math" -- that
    labeling in an earlier version of this class was itself part of the bug:
    it had the sourced/derived fields backwards). Each bone can still have a
    different number of keyframes covering a different total span within one
    clip -- that part was always true.

    Also ported from the same source pass (previously missing entirely):
    CAnimBlendSequence::RemoveQuaternionFlips -- consecutive keyframe
    quaternions aren't guaranteed to be on the same side of the quaternion
    double-cover, which without correction makes Godot's rotation track
    slerp the long way around between flipped pairs. See convert_z_up_to_y_up
    and the flip-removal loop in the .cpp for both fixes together — a real
    playtest with neither applied showed CJ badly, non-uniformly deformed
    (only ported over the RIGHT source once actual evidence of a bug came
    in, rather than re-guessing at coordinate math that was already correct).

    COORDINATE CONVENTION [DERIVED from a real playtest + verified
    numerically, not assumed up front]: rotation/translation are converted
    Z-up -> Y-up on the way in (convert_z_up_to_y_up() in the .cpp,
    identical transform to GTADffGeometry's vertex/normal conversion,
    Vector3(x,z,-y); for the quaternion this is applied to the vector part
    with w untouched, which is the mathematically exact re-expression for a
    proper-rotation change of basis like this one -- verified numerically
    against direct matrix conjugation, not just pattern-matched from the
    vector case).
    This class originally shipped assuming IFP keyframe data already shared
    GTADffSkeleton's bind poses' convention (no conversion), on the theory
    that both represent "bone-local orientation" from the same game. A real
    playtest proved that wrong -- CJ rendered badly deformed/lying down with
    no conversion applied. IFP animation data turns out to share DFF
    geometry's raw Z-up convention instead, despite being a completely
    unrelated file format/loader. Consistent with this project's repeated
    finding that coordinate convention has to be checked per data type, not
    inferred from what a same-named or conceptually-similar field turned out
    to need elsewhere.
    GTADffSkeleton's bind poses share that same raw Z-up convention too (an
    earlier measurement claiming otherwise was disproven by composing the
    real frame hierarchy -- see its header); it now applies the same change
    of basis in the conjugate form, so all three data types (vertices,
    skeleton rest, animation keyframes) live in one consistent Y-up space.

    ROTATION SPACE AND REST COMPOSITION [SOURCED, corrected after the
    4.7.1-binary composition semantics were measured]: the real engine
    REPLACES each bone's rest rotation with the animation quaternion every
    frame, and keeps the rest translation as the bone's hinge --
      - source/game_sa/Plugins/RpAnimBlendPlugin/RpAnimBlend.cpp,
        FrameUpdateCallBackCompressedSkinned / FrameUpdateCallBackSkinned:
          `fd->KeyFrame->q = nextQ;`                       (rotation replaced)
          `fd->KeyFrame->t = lerp(fd->BonePos, nextT, blend);` (hinge = BonePos
             for bones without animation translation)
      - the keyframe matrix then becomes the bone's local frame in the RW
        hierarchy (RwFrameUpdateHierarchy), i.e. rest rotation is NOT
        composed with the animation -- the animation IS the bone's local
        orientation.
    Godot's Skeleton3D keeps the pose buffer SEPARATE from rest: the pose
    buffer holds only the track value and the rest enters the final skin
    solely through the bind inverse (skin = global_pose * rest_global^-1,
    skeleton_3d.cpp). Two engine facts pin down the compensation:
      - The 4.7.1 official binary's Transform3D::operator* is NOT the
        standard composition the sources claim. Measured empirically
        (Node3D world transforms, Skeleton3D::get_bone_global_rest, GDScript
        Variant `*` all agree): a*b == (basis b.basis*a.basis,
        origin a.basis^T*b.origin + a.origin).
      - GTADffSkeleton::build_skeleton3d stores engine-corrected rests whose
        engine rest-chain reproduces the T-converted raw GTA globals T_i.
    build_animation() therefore derives per-key track values t_i(k) whose
    engine chain (global_pose = parent.global_pose * pose) reproduces
    P_i(k) = K_i(k) * T_i -- K_i(k) = T-converted raw keyframe global, so
    the final skin P_i * T_i^-1 equals the raw animation exactly:
      root: t = P;  else: basis = M(P_i)*M(P_par)^-1,
                          origin = M(P_par)*(P_i.o - P_par.o).
    (The old model here -- "Godot composes pose over rest, so write
    pose = anim*rest^-1" -- was wrong for this build: it assumed the 4.7.1
    operator* were the standard one, which the empirical measurement
    disproved.)

    ROOT BONE TRANSLATION [SOURCED, corrected after a real playtest surfaced
    it]: this class originally applied every sequence's translation keyframes
    -- including the "Root" bone's (eBoneTag 0) -- directly as a
    POSITION_3D track, on the assumption that "has_translation" meant "play
    this translation back visually", same as any other bone. A real playtest
    showed this was wrong in two visible ways at once: (1) the whole model
    drifted forward while walking, snapping back to the CharacterBody3D's
    actual position and re-drifting (faster each time) on every animation
    loop, and (2) the drift direction didn't match the character's actual
    facing/movement direction. Both are the SAME bug, not two: the Root
    bone's translation was being applied on top of GTAPedOnFoot's own
    movement (which already fully drives the CharacterBody3D -- see its own
    header), so the character was being moved twice by two uncoordinated
    systems, and the animation's own translation (still in the skeleton's
    unrotated local space) was never guaranteed to line up with whatever
    direction the CharacterBody3D node itself happened to be facing.

    The real engine never has this problem because it never visually moves
    the Root bone at all:
      - source/game_sa/Animation/AnimBlendFrameData.h, the HasVelocity flag:
        "If true the translation is used to move the ped", and
        source/game_sa/Plugins/RpAnimBlendPlugin/RpAnimBlend.cpp,
        RpAnimBlendClumpInit: `GetRootFrameData().HasVelocity = true` is set
        UNCONDITIONALLY on the clump's frame 0 (the Root bone) for every
        clump, ped or not.
      - RpAnimBlend.cpp's velocity-extraction frame-update path (e.g.
        FrameUpdateCallBackWithVelocityExtractionCompressedSkinned, ~line
        642) does two things with that bone's accumulated translation
        `deltaV`/`nextT` each frame, when `fd->HasVelocity &&
        gpAnimBlendClump->m_PedPosition` (always true for a CPed):
          `*wsPos += deltaV - currV;`                      // moves the ped
          `fd->KeyFrame->t = nextT - deltaV + fd->BonePos;` // visual bone
        Since `nextT` and `deltaV` are accumulated from the exact same
        per-node translation for a single playing association, `nextT -
        deltaV` cancels to (approximately, and exactly for a single
        non-blended animation) zero -- so the Root bone's own on-screen
        local translation is algebraically pinned to `fd->BonePos` (its
        bind/rest position) EVERY FRAME, regardless of what the animation
        file's translation keyframes contain. The engine always redirects
        that motion into moving the entity in world space instead of
        playing it back on the skeleton.
      - This matches the project architecture here: GTAPedOnFoot is already
        a from-scratch, code-driven CharacterBody3D mover (see its own
        header) -- it is the equivalent of the engine's `*wsPos +=
        deltaV - currV` step, just with placeholder speeds instead of
        animation-derived ones for now. The animation should behave like it
        does in the real engine: legs/arms/torso cycle normally, Root bone
        visually static.

    Fix: build_animation()'s new `strip_root_translation` parameter (default
    true) skips adding a POSITION_3D track for whichever sequence resolves
    to the "Root" bone (matched by eBoneTag 0 OR literal name "Root",
    case-insensitive, same dual match as everywhere else in this class) --
    its ROTATION_3D track still plays normally. Every other bone's
    translation (if any -- SOURCED comment on AnimBlendSequence.h's
    m_bHasTranslation field literally says "Root key frames have translation
    values", i.e. this is expected to be the only bone that ever has it for
    standard ped locomotion clips) is untouched.
*/
#ifndef GTA_IFP_ANIMATION_H
#define GTA_IFP_ANIMATION_H

#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class GTAIfpAnimation : public RefCounted {
	GDCLASS(GTAIfpAnimation, RefCounted)

public:
	struct Keyframe {
		Quaternion rotation; // Y-up (converted) and quaternion-flip-corrected -- see .cpp
		Vector3 translation; // only meaningful if the owning sequence has_translation; Y-up (converted)
		float delta_time = 0.0f; // [SOURCED, derived from on-disk cumulative value -- see class doc] seconds since the previous keyframe in this sequence
		float time = 0.0f; // [SOURCED, as-read] cumulative time from the start of this sequence
	};

	struct BoneSequence {
		String bone_name;   // resolved name (see class comment: bone_tag wins over the literal name if set)
		String raw_name;    // the literal 24-byte name field, always kept regardless of bone_tag
		int32_t bone_tag = -1; // eBoneTag value; -1 (BONE_UNKNOWN) means "use raw_name"
		bool has_translation = false;
		bool was_compressed = false; // frame_type 3/4 (dequantized already; this is just diagnostic info)
		std::vector<Keyframe> keyframes;
	};

	struct AnimClip {
		String name;
		std::vector<BoneSequence> sequences;
		float total_time = 0.0f; // [SOURCED, ICalcTotalTime] max, across all non-empty sequences, of that sequence's own last keyframe's cumulative time
	};

private:
	String block_name;
	std::vector<AnimClip> clips;

	static String _bone_tag_to_name(int32_t tag);
	const AnimClip *_find_clip(const String &clip_name) const;
	const BoneSequence *_find_sequence(const AnimClip &clip, const String &bone_name) const;

	bool _parse_anp23(const uint8_t *data, size_t size, bool is_anp3);

protected:
	static void _bind_methods();

public:
	GTAIfpAnimation();
	~GTAIfpAnimation() override;

	bool parse(const PackedByteArray &bytes);

	String get_block_name() const;
	int get_clip_count() const;
	PackedStringArray get_clip_names() const;

	// {"name", "bone_count", "total_time", "bones": [{"name","tag","frame_count","has_translation"}, ...]}
	Dictionary get_clip_info(const String &clip_name) const;

	// Array of {"time": float, "delta_time": float, "rotation": Quaternion, "translation": Vector3}
	Array get_bone_keyframes(const String &clip_name, const String &bone_name) const;

	// Builds a real Godot Animation resource (rotation_3d + position_3d bone
	// tracks) from a parsed clip, ready to hand to an AnimationPlayer whose
	// track paths point at `skeleton_track_path` (e.g. NodePath("Skeleton3D"),
	// relative to wherever the AnimationPlayer sits in your scene).
	//
	// If `skeleton` is given, each sequence's bone (bone_name, falling back to
	// raw_name -- same precedence as get_bone_keyframes' internal matching) is
	// looked up against the REAL skeleton via Skeleton3D::find_bone() before a
	// track is added for it, and any sequence that doesn't resolve to an
	// actual bone on that skeleton is skipped with a push_warning naming it,
	// rather than silently emitting a track that will never play. This project
	// has repeatedly found that a name/index scheme "obviously" matching
	// between two file formats didn't (see gta_character_visual.gd's
	// skin-index notes) -- treat this per-clip match/skip count as something
	// to actually check, not assume, the first time you run this against a
	// real ped.ifp. Pass null to skip the check and emit every track
	// unconditionally (useful for inspecting raw data, not recommended for
	// actual playback).
	// [SOURCED, see .cpp for the exact citation] strip_root_translation
	// (default true) drops the POSITION_3D track for the "Root" bone
	// (eBoneTag 0) specifically -- the real engine NEVER visually moves that
	// bone; it always redirects its baked-in translation into moving the
	// CPed's world position instead, and pins the bone's own local
	// translation to its bind pose every single frame. Leave this true for
	// any animation actually driving a playable, code-moved character (e.g.
	// GTAPedOnFoot, which already computes its own movement); only pass
	// false for raw inspection/diagnostic dumps where you want to see the
	// untouched on-disk data.
	Ref<Animation> build_animation(const String &clip_name, const NodePath &skeleton_track_path, Skeleton3D *skeleton = nullptr, bool strip_root_translation = true) const;
};

} // namespace godot

#endif // GTA_IFP_ANIMATION_H
