/*
    Shared bone-tag -> name table.

    [SOURCED] source/game_sa/Enums/eBoneTag.h (gta-reversed).

    Used by both GTAIfpAnimation (bone_tag field on IFP sequences) and
    GTADffSkeleton (HAnim node IDs read from the .dff's skeleton) — they're
    the same ID space, which is exactly what lets us match an animation
    sequence to the correct skeleton bone.
*/
#ifndef GTA_BONE_TAGS_H
#define GTA_BONE_TAGS_H

#include <godot_cpp/variant/string.hpp>

namespace godot {

inline String gta_bone_tag_to_name(int32_t tag) {
	switch (tag) {
		case 0: return String("Root");
		case 1: return String("Pelvis");
		case 2: return String("Spine");
		case 3: return String("Spine1");
		case 4: return String("Neck");
		case 5: return String("Head");
		case 6: return String("L Brow");
		case 7: return String("R Brow");
		case 8: return String("Jaw");
		case 21: return String("R Clavicle");
		case 22: return String("R UpperArm");
		case 23: return String("R ForeArm");
		case 24: return String("R Hand");
		case 25: return String("R Finger");
		case 26: return String("R Finger01");
		case 31: return String("L Clavicle");
		case 32: return String("L UpperArm");
		case 33: return String("L ForeArm");
		case 34: return String("L Hand");
		case 35: return String("L Finger");
		case 36: return String("L Finger01");
		case 41: return String("L Thigh");
		case 42: return String("L Calf");
		case 43: return String("L Foot");
		case 44: return String("L Toe0");
		case 51: return String("R Thigh");
		case 52: return String("R Calf");
		case 53: return String("R Foot");
		case 54: return String("R Toe0");
		case 201: return String("Belly");
		case 301: return String("R Breast");
		case 302: return String("L Breast");
		default: return String("BoneTag_") + String::num_int64(tag);
	}
}

} // namespace godot

#endif // GTA_BONE_TAGS_H
