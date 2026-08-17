class_name GTACharacterVisual
extends Node3D

## Assembles the fully-skinned, fully-textured CJ model (shared 33-bone
## Skeleton3D + 5 body-part MeshInstance3Ds) as children of this node.
##
## This is the SAME pipeline validated standalone in assemble_cj.gd, moved
## into a reusable component so it can be attached directly under
## GTAPedOnFoot (see priority #1 in NEXT_STEPS: "wire the assembled/skinned
## CJ mesh onto GTAPedOnFoot"). assemble_cj.gd itself now just instantiates
## this under a bare Node3D for standalone testing -- there is exactly one
## copy of the assembly logic, here.
##
## Usage: add as a child of GTAPedOnFoot (or any Node3D) in a scene, or
## instantiate at runtime -- it assembles itself in _ready(). get_skeleton()
## returns the built Skeleton3D once "assembled" has fired.

signal assembled(skeleton: Skeleton3D)

## Case-insensitive .dff basename -> real texture-source .txd name.
## [SOURCED empirically, see NEXT_STEPS key finding #6] head.dff pairs with
## player_face.txd, NOT head.txd/head8bit (that's unused leftover Rockstar
## dev data still present in the shipped files). legs/torso/feet use the
## "player_*"/"foot" naming instead of a direct basename+".txd" match.
## hands.dff is the one part where basename+".txd" happens to be correct,
## so it's not listed here. Worth re-checking if hands/feet clothing
## variants ever look wrong the same way head did (noted as unconfirmed but
## not ruled out).
const TXD_OVERRIDES := {
	"torso.dff": "player_torso.txd",
	"legs.dff": "player_legs.txd",
	"head.dff": "player_face.txd",
	"feet.dff": "foot.txd",
}

## clip key (used with AnimationPlayer.play()) -> real ped.ifp clip name.
## [SOURCED] gta-reversed source/game_sa/Animation/AnimAssocAnimations.h,
## aPlayerAnimations[] -- specifically the table AnimAssocDefinitions.cpp
## wires to group "player"/block "ped" (i.e. THIS file, anim/ped.ifp), which
## is the player-controlled ped, not aStdAnimations (generic civilian NPCs,
## different clip names -- "Walk_civi"/"run_civi" instead of these).
const ANIM_CLIP_SOURCE := {
	"idle": "IDLE_STANCE",
	"walk": "walk_player",
	"run": "run_player",
	"sprint": "SPRINT_civi",
	"jump_launch": "JUMP_launch",
	"jump_glide": "JUMP_glide",
	"jump_land": "JUMP_land",
	"fall_land": "FALL_land",
	"fall_fall": "FALL_fall",
	"fall_collapse": "FALL_collapse",
	"ko_skid_back": "KO_skid_back",
	"getup": "getup",
}

## One-shot clips. Everything not listed here loops (the game's action
## animations repeat until a task switches them; the one-shots are the
## launch/land/recover anims the engine waits on -- e.g. CTaskSimpleLand
## holds until the anim finishes, TaskSimpleGetUp the same).
const ONE_SHOT_CLIPS := ["jump_launch", "jump_land", "fall_land", "fall_collapse", "ko_skid_back", "getup"]

## Directory containing gta3.img / player.img (and everything else under
## GTA:SA's models/ folder). Defaults to the project-local res://gta/models.
@export_dir var gta_models_path: String = "res://gta/models"

## player.dff (the shared 33-bone skeleton) lives in gta3.img, not player.img
## -- see NEXT_STEPS key finding #1.
@export var base_archive: String = "gta3.img"
@export var base_dff: String = "player.dff"

## The five default body parts live in player.img alongside many unused
## clothing/hair variants (see NEXT_STEPS priority #6 -- not touched yet).
@export var parts_archive: String = "player.img"
@export var body_parts: PackedStringArray = [
	"legs.dff", "torso.dff", "head.dff", "hands.dff", "feet.dff",
]

## Print per-part diagnostics (hierarchy tables, bone coverage audits) to
## the console. Off by default so this doesn't spam the log every time a
## GTAPedOnFoot scene loads; flip on when debugging a specific part.
@export var verbose: bool = false

## Fine-tune on top of the automatic feet alignment below (e.g. if a
## specific part's lowest vertex -- a shoe sole, a toe -- sits slightly off
## from where you want ground contact). Usually 0.
@export var root_offset: Vector3 = Vector3.ZERO

## Base pitch rotation (X=-90) that aligns RenderWare space (Z up, Y forward)
## to Godot space (Y up, Z forward).
@export var model_pitch_degrees: float = -90.0

## Rotates the assembled model around Y (yaw) on top of the base pitch rotation.
@export var model_forward_correction_degrees: float = 180.0


## Loads anim/ped.ifp (loose file, NOT in an .img -- see GTAIfpAnimation's
## header) and builds walk/run/sprint/idle as an AnimationPlayer once the
## skeleton exists. If this node's parent is a GTAPedOnFoot, playback is
## driven automatically off its move_state_changed signal; if not (e.g. the
## standalone assemble_cj.gd test scene), it just plays "idle" once and
## sits there.
@export var play_animations: bool = true

## Empty = auto-derive as gta_models_path's parent + "anim/ped.ifp" -- GTA:SA's
## own on-disk layout has anim/ alongside models/, both directly under the
## install root, so gta_models_path's parent IS that root.
@export var anim_ifp_path: String = ""

## Blend time for animation transitions. Blending happens ONLY inside these
## transition windows -- playback itself is the authored keyframes. GTA:SA's
## own blend deltas are slower (CPed::UpdateMoveAnim: IDLE 4.0/s, WALK/RUN/
## SPRINT 1.0/s, TURN 16.0/s), but a short fade keeps state changes readable.
@export var anim_crossfade_sec: float = 0.1

var skel_parser: GTADffSkeleton
var skel: Skeleton3D
var img: GTAImgArchive
var anim_player: AnimationPlayer
var _lowest_vertex_y: float = 0.0
var _found_any_vertex: bool = false


func _ready() -> void:
	assemble()


## Does the full build: shared skeleton, then each body part skinned +
## textured onto it. Returns the built Skeleton3D (also available afterward
## via get_skeleton()). Safe to call more than once -- clears any previous
## build first.
func assemble() -> Skeleton3D:
	if skel:
		skel.queue_free()
		skel = null

	rotation_degrees = Vector3(model_pitch_degrees, model_forward_correction_degrees, 0.0)

	var img_base := GTAImgArchive.new()
	if not img_base.open(gta_models_path.path_join(base_archive)):
		push_error("GTACharacterVisual: failed to open " + base_archive + " at " + gta_models_path)
		return null

	skel_parser = GTADffSkeleton.new()
	if not skel_parser.parse(img_base.extract_file(base_dff)):
		push_error("GTACharacterVisual: failed to parse " + base_dff + " skeleton")
		return null

	skel = skel_parser.build_skeleton3d(false)
	skel.name = "Skeleton3D"
	add_child(skel)

	if verbose:
		print("CJ skeleton: ", skel_parser.get_bone_count(), " bones (from ", base_dff, ")")

	img = GTAImgArchive.new()
	if not img.open(gta_models_path.path_join(parts_archive)):
		push_error("GTACharacterVisual: failed to open " + parts_archive + " at " + gta_models_path)
		return skel

	_found_any_vertex = false
	_lowest_vertex_y = 0.0
	for part in body_parts:
		_attach_part(part)

	# The Root bone's authored rest is ~180deg from its animated value (the
	# canonical rotation all IFP clips are hemisphere-aligned to). Godot's
	# AnimationPlayer blends every frame starting from each bone's REST pose,
	# so any crossfade sweeps the root through the rest (measured 173deg
	# swing). Override the Root's rest to the canonical rotation so crossfades
	# blend near the values instead. Capture an explicit Skin from the ORIGINAL
	# rest first: the auto-generated skin would otherwise bind against the new
	# rest and rotate the whole model.
	var bind_skin: Skin = skel.create_skin_from_rest_transforms()
	for child in skel.get_children():
		var mi := child as MeshInstance3D
		if mi:
			mi.skin = bind_skin
	var root_idx := skel.find_bone("Root")
	if root_idx != -1:
		var root_rest: Transform3D = skel.get_bone_rest(root_idx)
		root_rest.basis = Basis(Quaternion(0.0, 0.0, 0.70710678, 0.70710678))
		skel.set_bone_rest(root_idx, root_rest)
		if verbose:
			print("Root bone rest overridden to canonical rotation (crossfade blend base)")

	# Auto-align feet to ground: with X=-90 pitch, the skeleton's Z axis maps to Godot world +Y.
	# Shifting the skeleton along Z by -_lowest_vertex_y places shoe soles at local origin (y=0).
	if _found_any_vertex:
		skel.position.z = root_offset.z - _lowest_vertex_y
		if verbose:
			print(
				"CJ feet auto-align: lowest vertex y=", _lowest_vertex_y,
				" -> skel.position.z=", skel.position.z,
			)
	else:
		skel.position.z = root_offset.z
	skel.position.x = root_offset.x
	skel.position.y = root_offset.y
	# Capture the base offset AFTER the feet alignment: the fall root-motion
	# integration and its recovery ease both operate relative to this.
	_base_skel_position = skel.position

	if verbose:
		print("CJ assembled: ", ", ".join(body_parts), " skinned to the shared skeleton.")

	_load_animations()

	assembled.emit(skel)
	return skel


func get_skeleton() -> Skeleton3D:
	return skel


func _resolve_anim_ifp_path() -> String:
	if not anim_ifp_path.is_empty():
		return anim_ifp_path
	return gta_models_path.get_base_dir().path_join("anim/ped.ifp")


## Reads anim/ped.ifp, builds the ANIM_CLIP_SOURCE clips against the just-
## assembled skeleton, and wires an AnimationPlayer to GTAPedOnFoot's
## move_state_changed signal if our parent fires one. See build_animation()'s
## own doc comment in gta_ifp_animation.h: it reports a per-clip matched/
## skipped bone count via print/push_warning -- check the console the first
## time this runs against a real file, since whether ped.ifp's bone-sequence
## names line up with player.dff's skeleton bone names hasn't been verified.
func _load_animations() -> void:
	if not play_animations:
		return

	var ifp_path := _resolve_anim_ifp_path()
	if not FileAccess.file_exists(ifp_path):
		push_warning("GTACharacterVisual: ped.ifp not found at ", ifp_path, " -- no animations loaded")
		return

	var bytes := FileAccess.get_file_as_bytes(ifp_path)
	var ifp := GTAIfpAnimation.new()
	if not ifp.parse(bytes):
		push_warning("GTACharacterVisual: failed to parse ", ifp_path)
		return

	var lib := AnimationLibrary.new()
	var any_built := false
	for clip_key in ANIM_CLIP_SOURCE:
		var source_name: String = ANIM_CLIP_SOURCE[clip_key]
		var anim: Animation = ifp.build_animation(source_name, NodePath(skel.name), skel)
		if anim == null:
			push_warning("GTACharacterVisual: clip '", source_name, "' not found in ", ifp_path)
			continue
		if clip_key in ONE_SHOT_CLIPS:
			anim.loop_mode = Animation.LOOP_NONE
		lib.add_animation(clip_key, anim)
		any_built = true

	if not any_built:
		push_warning("GTACharacterVisual: no animation clips built from ", ifp_path)
		return

	anim_player = AnimationPlayer.new()
	anim_player.name = "AnimationPlayer"
	anim_player.process_priority = 1
	add_child(anim_player)
	anim_player.add_animation_library("", lib)
	anim_player.animation_finished.connect(_on_animation_finished)

	var ped := get_parent() as GTAPedOnFoot
	if ped:
		# [SOURCED behavior] GTA:SA has no fixed on-foot speed constants -- a
		# ped's speed IS the playing animation's Root-bone translation (the
		# engine extracts the per-frame root position delta into
		# m_vecAnimMovingShiftLocal and integrates it into m_vecMoveSpeed;
		# gta-reversed RpAnimBlend.cpp FrameUpdateCallBackT around 0x4D34F0,
		# CPed::ProcessControl; SanAndreasUnity via PedModel.Velocity).
		# Placeholder speeds (run 4.0 vs the clip's 5.7 stride) made the feet
		# slide and the torso read as a gliding plank. Derive each state's
		# speed from its clip's root translation delta over one loop instead.
		var stride_speeds := {
			"walk": "walk_player",
			"run": "run_player",
			"sprint": "SPRINT_civi",
		}
		for move_key in stride_speeds:
			var source_name: String = stride_speeds[move_key]
			var root_kfs: Array = ifp.get_bone_keyframes(source_name, "Root")
			if root_kfs.size() < 2:
				continue
			var delta: Vector3 = root_kfs[root_kfs.size() - 1]["translation"] - root_kfs[0]["translation"]
			var loop_time: float = root_kfs[root_kfs.size() - 1]["time"] - root_kfs[0]["time"]
			if loop_time <= 0.0:
				continue
			var stride: float = delta.length() / loop_time
			match move_key:
				"walk":
					ped.walk_speed = stride
				"run":
					ped.run_speed = stride
				"sprint":
					ped.sprint_speed = stride
		# [SOURCED] The Root bone's translation is stripped from every built
		# Animation (gta_ifp_animation.cpp strip_root_translation: the game
		# redirects it into ped movement, never a pose). The fall-sequence
		# clips carry REAL authored root motion though -- KO_skid_back drops
		# the body ~0.9m to the floor, getup rises it back to standing,
		# FALL_collapse slides ~2.5m forward -- so keep their Root keyframes
		# here to integrate by hand in _process.
		_fall_root_kfs = {}
		for fall_key in ["ko_skid_back", "getup", "fall_collapse"]:
			var root_kfs: Array = ifp.get_bone_keyframes(ANIM_CLIP_SOURCE[fall_key], "Root")
			if root_kfs.size() >= 2:
				_fall_root_kfs[fall_key] = root_kfs

		ped.move_state_changed.connect(_on_move_state_changed)
		ped.jumped.connect(_on_jumped)
		ped.landed.connect(_on_landed)
		ped.fall_started.connect(_on_fall_started)
		ped.fall_landed_hard.connect(_on_fall_landed_hard)
		ped.fall_landed_soft.connect(_on_fall_landed_soft)
		ped.fall_getup_started.connect(_on_fall_getup_started)
		_play_move_state(ped.get_move_state(), true)
	elif anim_player.has_animation("idle"):
		anim_player.play("idle")
	# Apply frame 0 synchronously: the AnimationPlayer's first _process runs
	# NEXT frame, so without this the first rendered frame would show every
	# bone at its rest pose. The spawn play must not blend.
	anim_player.advance(0.0)

	if verbose:
		_last_state_change_time = Time.get_ticks_msec() / 1000.0


## LIVE debug, requested directly: every earlier "does the pose look sane"
## check (raw keyframes, T-pose rest agreement, chain-length reach) was run
## in an ISOLATED test scene -- a freshly built skeleton + a freshly built
## Animation, nothing to do with what's actually driving the real character.
## None of that exercises the one thing genuinely untested: this script's
## own crossfade blending (anim_player.play(clip_key, anim_crossfade_sec))
## on the REAL running Skeleton3D with all 5 parts attached simultaneously.
## This runs the exact same rigid chain-length reach check as
## global_pose_sanity_check.gd, but against the live skel node every ~0.5s,
## plus logs every move_state transition with the time since the previous
## one -- if crossfades are re-triggering before finishing (e.g. state
## flapping), that's the first thing this will show.
var _last_state_change_time := 0.0
var _debug_accum := 0.0
const _DEBUG_INTERVAL := 0.5
const _DEBUG_CHAINS := {
	"L arm (Clavicle->Hand)": ["L Clavicle", "L UpperArm", "L ForeArm", "L Hand"],
	"R arm (Clavicle->Hand)": ["R Clavicle", "R UpperArm", "R ForeArm", "R Hand"],
}

func _process(delta: float) -> void:
	_update_shoulder_bones()

	# [SOURCED] GTA:SA integrates the playing animation's Root-bone
	# translation into the ped's position every frame (the same mechanism
	# the stride speeds above derive from). The fall-sequence anims carry
	# real authored root motion -- KO_skid_back drops the body ~0.9m to the
	# floor, getup rises it back to standing, FALL_collapse drops ~0.6m
	# while sliding ~2.5m forward -- and without it the poses play at
	# standing height (the floaty "levitating" get-up). The stripped Root
	# translation is sampled from the IFP keyframes and integrated as a
	# per-frame delta into skel.position (same space); horizontal drift is
	# transferred to the ped at fall end so the collider catches up with
	# the visible body.
	if _fall_root_active and anim_player and skel:
		var root_origin: Vector3 = _sample_fall_root(
			anim_player.current_animation, anim_player.current_animation_position
		)
		var root_delta: Vector3 = root_origin - _fall_root_prev
		if anim_player.current_animation == &"fall_collapse":
			# [PLACEHOLDER] FALL_collapse's authored ~2.5m forward slide is
			# dropped -- the player wants static landings -- only the drop
			# (z, up axis) applies; the body collapses in place.
			skel.position.z += root_delta.z
		else:
			skel.position += root_delta
		_fall_root_prev = root_origin
	if _skeleton_reset > 0.0 and skel:
		# Only the height (z) returns to base -- the horizontal accumulation
		# was already transferred to the ped (see _on_animation_finished).
		skel.position.z = lerpf(skel.position.z, _base_skel_position.z, min(1.0, delta * 4.0))
		_skeleton_reset -= delta
		if _skeleton_reset <= 0.0:
			skel.position.z = _base_skel_position.z

	if not verbose or not skel or not anim_player:
		return
	_debug_accum += delta
	if _debug_accum < _DEBUG_INTERVAL:
		return
	_debug_accum = 0.0

	var since_last_change := (Time.get_ticks_msec() / 1000.0) - _last_state_change_time
	print(
		"[live] anim='", anim_player.current_animation, "' pos=", "%.2f" % anim_player.current_animation_position,
		" mid-crossfade=", (since_last_change < anim_crossfade_sec),
		" (", "%.2f" % since_last_change, "s since last state change, crossfade=", anim_crossfade_sec, "s)",
	)
	for chain_name in _DEBUG_CHAINS:
		var bones: Array = _DEBUG_CHAINS[chain_name]
		var start_idx := skel.find_bone(bones[0])
		var end_idx := skel.find_bone(bones[bones.size() - 1])
		if start_idx == -1 or end_idx == -1:
			continue
		var start_global := skel.get_bone_global_pose(start_idx).origin
		var end_global := skel.get_bone_global_pose(end_idx).origin
		var posed_dist: float = (end_global - start_global).length()
		var rest_len := 0.0
		for i in range(1, bones.size()):
			var idx := skel.find_bone(bones[i])
			if idx != -1:
				rest_len += skel.get_bone_rest(idx).origin.length()
		var flag := "  <<< IMPOSSIBLE" if posed_dist > rest_len + 0.01 else ""
		print("  ", chain_name, ": live posed=", "%.4f" % posed_dist, "m  max=", "%.4f" % rest_len, "m", flag)


var _is_jumping: bool = false
var _is_falling: bool = false

## Root-motion integration for the fall sequence anims (see _process).
var _fall_root_active: bool = false
var _fall_root_prev: Vector3 = Vector3.ZERO
var _base_skel_position: Vector3 = Vector3.ZERO
var _skeleton_reset: float = 0.0
var _fall_root_kfs: Dictionary = {}


## [SOURCED] CPed::ShoulderBoneRotation (gta-reversed
## source/game_sa/Entity/Ped/Ped.cpp @ 0x5DF560): GTA:SA never plays the
## IFP's L/R Breast keyframes (build_animation in gta_ifp_animation.cpp
## skips them) -- instead it derives each Breast bone's matrix every frame
## from the Clavicle and UpperArm poses:
##     breast = clavicle * halfX(clavicle^-1 * upperArm)
## where halfX() is a full CMatrix euler round-trip (Matrix.cpp @ 0x59A840
## ConvertToEulerAngles / @ 0x59AA40 ConvertFromEulerAngles) with the
## ORDER_ZYX | SWAP_XZ flags, halving the X angle in between. Without this
## the shoulder mesh (skinned mostly to the Breast bones, verified via
## torso.dff vertex weights) freezes at rest while the arms animate.
##
## Done in skeleton space: Godot's bone global poses compose the same way
## RW's clump-space matrices do (rest local * parent chain), and the
## visual's own -90/-180 rotation sits ABOVE the skeleton, so it cancels
## out of the relative math below.
func _update_shoulder_bones() -> void:
	if not skel:
		return
	# Compute bone GLOBAL poses manually by composing local poses up the
	# parent chain. Godot 4.5+ get_bone_global_pose()/set_bone_global_pose()
	# are unreliable (godotengine/godot#103623: global-pose cache doesn't
	# refresh), and the AnimationPlayer only ever writes LOCAL poses to the
	# pose buffer, so this side-steps the bug entirely and always agrees with
	# what the animator just wrote.
	var globals := {}
	for b in skel.get_bone_count():
		var parent := skel.get_bone_parent(b)
		var local := skel.get_bone_pose(b)
		globals[b] = local if parent == -1 else globals[parent] * local
	for side in ["L", "R"]:
		var clav_idx := skel.find_bone(side + " Clavicle")
		var arm_idx := skel.find_bone(side + " UpperArm")
		var breast_idx := skel.find_bone(side + " Breast")
		if clav_idx == -1 or arm_idx == -1 or breast_idx == -1:
			continue
		var clav_g: Transform3D = globals[clav_idx]
		var arm_g: Transform3D = globals[arm_idx]
		var rel := clav_g.affine_inverse() * arm_g
		rel.basis = _half_x_rotation(rel.basis)
		var breast_global := clav_g * rel
		var parent := skel.get_bone_parent(breast_idx)
		var breast_local: Transform3D = breast_global if parent == -1 else globals[parent].affine_inverse() * breast_global
		skel.set_bone_pose(breast_idx, breast_local)


## [SOURCED] CMatrix::ConvertToEulerAngles (Matrix.cpp @ 0x59A840) followed
## by halving the X angle, then CMatrix::ConvertFromEulerAngles (Matrix.cpp
## @ 0x59AA40), with the exact flags CPed::ShoulderBoneRotation uses:
## ORDER_ZYX (iInd1=2, iInd2=1, iInd3=0) | SWAP_XZ (0x01) | Tait-Bryan
## (EULER_ANGLES unset), ORDER_ZYX's _ORDER_NEEDS_SWAP (0x04) negation in
## both directions.
##
## Column mapping: CMatrix aliases the RwMatrix memory directly (Matrix.h
## @0x0/0x10/0x20): fArr[0]=m_right=RW right row, fArr[1]=m_forward=RW up row,
## fArr[2]=m_up=RW at row. Our skeleton stores the raw frame as
## Transform3D(right, up, at) (see GTADffSkeleton), so basis.x=right,
## basis.y=up, basis.z=at. Hence fArr row 0 = basis.x, row 1 = basis.y,
## row 2 = basis.z.
func _half_x_rotation(b: Basis) -> Basis:
	var r0 := b.x
	var fw := b.y
	var up := b.z

	# --- ConvertToEulerAngles (Tait-Bryan, ORDER_ZYX) ---
	# r21 = fArr[1][2] (up.z), r11 = fArr[2][2] (at.z)
	var r21 := fw.z
	var r11 := up.z
	var cy := sqrt(r11 * r11 + r21 * r21)
	var px: float
	var py: float
	var pz: float
	if cy > 0.0000019073486:
		# pX = atan2(fArr[0][1], fArr[0][0]), pY = atan2(-fArr[0][2], cy)
		px = atan2(r0.y, r0.x)
		py = atan2(-r0.z, cy)
		pz = atan2(r21, r11)
	else:
		# pX = atan2(-fArr[1][2], fArr[1][1]), pY = atan2(-fArr[0][2], cy)
		px = atan2(-fw.z, fw.y)
		py = atan2(-r0.z, cy)
		pz = 0.0

	# SWAP_XZ
	var tmp := px
	px = pz
	pz = tmp
	# _ORDER_NEEDS_SWAP (ORDER_ZYX): negate all angles
	px = -px
	py = -py
	pz = -pz

	# half it's X rotation
	px *= 0.5

	# ConvertFromEulerAngles: SWAP_XZ then _ORDER_NEEDS_SWAP again
	tmp = px
	px = pz
	pz = tmp
	px = -px
	py = -py
	pz = -pz

	var sx := sin(px)
	var cx := cos(px)
	var sy := sin(py)
	var cyy := cos(py)
	var sz := sin(pz)
	var cz := cos(pz)

	# --- Tait-Bryan recompose, ORDER_ZYX: iInd1=2, iInd2=1, iInd3=0 ---
	# right row (fArr[0]) = basis.x, up row (fArr[1]) = basis.y,
	# at row (fArr[2]) = basis.z
	var o00 := cx * cyy   # right.x
	var o01 := cyy * sx   # right.y
	var o02 := -sy        # right.z
	var o10 := -(cz * sx) + cx * sy * sz  # up.x
	var o11 := cx * cz + sx * sy * sz     # up.y
	var o12 := cyy * sz                   # up.z
	var o20 := sx * sz + cx * cz * sy     # at.x
	var o21 := -(cx * sz) + cz * sx * sy  # at.y
	var o22 := cx * cyy                   # at.z

	return Basis(
		Vector3(o00, o01, o02),
		Vector3(o10, o11, o12),
		Vector3(o20, o21, o22),
	)


func _sample_fall_root(clip_key: String, t: float) -> Vector3:
	var kfs: Array = _fall_root_kfs.get(clip_key, [])
	if kfs.is_empty():
		return Vector3.ZERO
	var last_idx := kfs.size() - 1
	if t >= kfs[last_idx]["time"]:
		return kfs[last_idx]["translation"]
	for i in range(1, kfs.size()):
		if kfs[i]["time"] >= t:
			var a: Dictionary = kfs[i - 1]
			var b: Dictionary = kfs[i]
			var span: float = b["time"] - a["time"]
			var w: float = 0.0 if span <= 0.0 else (t - a["time"]) / span
			return (a["translation"] as Vector3).lerp(b["translation"], w)
	return kfs[last_idx]["translation"]


func _play_move_state(state: int, p_instant := false) -> void:
	if not anim_player:
		return
	var clip_key := "idle"
	match state:
		GTAPedOnFoot.PEDMOVE_WALK:
			clip_key = "walk"
		GTAPedOnFoot.PEDMOVE_JOG, GTAPedOnFoot.PEDMOVE_RUN:
			clip_key = "run"
		GTAPedOnFoot.PEDMOVE_SPRINT:
			clip_key = "sprint"
	# p_instant is ONLY for the very first play at spawn: there is nothing to
	# blend FROM yet. Every real state change below uses the crossfade.
	var blend_time: float = 0.0 if p_instant else anim_crossfade_sec
	if anim_player.has_animation(clip_key) and anim_player.current_animation != clip_key:
		anim_player.play(clip_key, blend_time)


## GTAPedOnFoot.move_state_changed handler -- picks the clip key for the new
## PedMoveState. Only WALK/JOG/RUN/SPRINT are ever actually emitted today
## (see GTAPedOnFoot::_update_move_state); TURN_L/TURN_R/STILL/NONE all fall
## through to idle for now -- turning-in-place has its own animations in the
## real game (ANIM_ID_TURN_L/R) that aren't wired up yet, same simplification
## noted in GTAPedOnFoot's own header for turn_rate.
func _on_move_state_changed(new_state: int, _old_state: int) -> void:
	if verbose:
		var now := Time.get_ticks_msec() / 1000.0
		print(
			"[live] move_state_changed -> ", new_state, ", ",
			"%.2f" % (now - _last_state_change_time), "s since previous change",
			(" <<< RAPID (crossfade may not have finished)" if (now - _last_state_change_time) < anim_crossfade_sec else ""),
		)
		_last_state_change_time = now
	if not _is_jumping and not _is_falling:
		_play_move_state(new_state)


func _on_jumped() -> void:
	if not anim_player:
		return
	_is_jumping = true
	# Jump transitions crossfade like every other state change: instant
	# swaps here made the launch/glide/land chain look cut off (the body
	# popped from the run pose into the launch and the land popped in over
	# the glide).
	if anim_player.has_animation("jump_launch"):
		anim_player.play("jump_launch", anim_crossfade_sec)
	elif anim_player.has_animation("jump_glide"):
		anim_player.play("jump_glide", anim_crossfade_sec)


func _on_landed() -> void:
	if not anim_player:
		return
	# [SOURCED] TaskComplexInAirAndLand.cpp:78-90: a feet-first landing plays
	# JUMP_LAND while moving, FALL_LAND otherwise (the game's blend-ratio
	# scale is 0..3, ours 0..1, so "past half intensity" is 0.5 here; the
	# ratio only rises with held input, same as the game's pad check).
	var ped := get_parent() as GTAPedOnFoot
	var moving: bool = ped != null and ped.get_move_blend_ratio() > 0.5
	var clip_key := "jump_land" if moving else "fall_land"
	if anim_player.has_animation(clip_key):
		anim_player.play(clip_key, anim_crossfade_sec)
	elif anim_player.has_animation("jump_land"):
		anim_player.play("jump_land", anim_crossfade_sec)
	else:
		_is_jumping = false
		if ped:
			_play_move_state(ped.get_move_state())
		else:
			anim_player.play("idle", anim_crossfade_sec)


## [SOURCED] TaskSimpleInAir::ProcessPed (0x680600): the FALL_FALL arms-out
## flail starts when the descent exceeds -5 units/sec while there is no
## ground within 4 units below -- it plays (looping) until touchdown. A
## normal jump's descent never meets the free-fall condition, so only real
## falls flail.
func _on_fall_started(_fall_speed: float) -> void:
	if not anim_player:
		return
	_is_jumping = false
	_is_falling = true
	if anim_player.has_animation("fall_fall"):
		anim_player.play("fall_fall", anim_crossfade_sec)


## [SOURCED] TaskComplexInAirAndLand::CreateNextSubTask (0x67CCB0): a flail
## landing with min fall speed < -20 units/sec becomes
## CTaskSimpleFall(ANIM_ID_KO_SKID_BACK, ANIM_GROUP_DEFAULT, 700) -- the KO
## skid plays for the 700ms down time (the ped times that itself and fires
## fall_getup_started), then the ped gets up.
func _on_fall_landed_hard(_min_speed: float) -> void:
	if not anim_player:
		return
	_fall_root_active = true
	_fall_root_prev = _sample_fall_root("ko_skid_back", 0.0)
	if anim_player.has_animation("ko_skid_back"):
		anim_player.play("ko_skid_back", anim_crossfade_sec)


## [SOURCED] Same function: a flail landing slower than the KO threshold
## becomes CTaskSimpleLand(ANIM_ID_FALL_COLLAPSE) -- the collapse plays once,
## then (notify_fall_recovered from _on_animation_finished) the ped stands.
func _on_fall_landed_soft(_min_speed: float) -> void:
	if not anim_player:
		return
	_fall_root_active = true
	_fall_root_prev = _sample_fall_root("fall_collapse", 0.0)
	if anim_player.has_animation("fall_collapse"):
		anim_player.play("fall_collapse", anim_crossfade_sec)


## [SOURCED] TaskComplexInAirAndLand.cpp:46-47 + TaskSimpleGetUp::StartAnim
## (0x67C770): after the fall task a get-up task starts, playing GETUP_0
## ("getup" -- the back get-up; GETUP_FRONT only when a front anim is
## associated, which our KO_SKID_BACK fall never is).
func _on_fall_getup_started() -> void:
	if not anim_player:
		return
	if anim_player.has_animation("getup"):
		anim_player.play("getup", anim_crossfade_sec)


func _on_animation_finished(anim_name: StringName) -> void:
	if not anim_player:
		return
	if anim_name == &"jump_launch":
		if _is_jumping and anim_player.has_animation("jump_glide"):
			anim_player.play("jump_glide", anim_crossfade_sec)
	elif anim_name == &"jump_land" or anim_name == &"fall_land":
		_is_jumping = false
		var ped := get_parent() as GTAPedOnFoot
		if ped:
			_play_move_state(ped.get_move_state())
		else:
			anim_player.play("idle", anim_crossfade_sec)
	elif anim_name == &"fall_collapse" or anim_name == &"getup":
		# [SOURCED shape] FinishGetUpAnimCB (0x678110) / CTaskSimpleLand
		# finishing: the fall sequence is over, the move task resumes --
		# the visual tells the ped the input lock can drop.
		_is_falling = false
		_fall_root_active = false
		# The Root-bone pose now starts the move anim's loop origin, so the
		# skeleton's accumulated fall drift is visible only as position.
		# Hand the horizontal part to the ped (its collider stayed put while
		# the locked body slid/lied) and ease the height back to base.
		var ped := get_parent() as GTAPedOnFoot
		if ped and skel:
			var drift := Vector3(
				skel.position.x - _base_skel_position.x,
				0.0,
				skel.position.y - _base_skel_position.y,
			)
			if drift.length() > 0.001:
				ped.global_position += drift
			skel.position.x = _base_skel_position.x
			skel.position.y = _base_skel_position.y
			_skeleton_reset = 0.3
			ped.notify_fall_recovered()
			_play_move_state(ped.get_move_state())
		else:
			if skel:
				skel.position = _base_skel_position
			anim_player.play("idle", anim_crossfade_sec)


func _attach_part(dff_name: String) -> void:
	var bytes: PackedByteArray = img.extract_file(dff_name)
	if bytes.is_empty():
		push_warning("GTACharacterVisual: " + dff_name + " not found in " + parts_archive)
		return

	# Each clothing piece embeds its OWN frame list/skeleton, with its own
	# local bone ordering -- confirmed this does NOT match player.dff's
	# ordering past the first ~20 bones. So decode each mesh's vertex bone
	# indices against ITS OWN embedded skeleton, then resolve final names
	# against the shared runtime skeleton.
	var own_skel := GTADffSkeleton.new()
	var slot_to_bone_name: Dictionary = {}
	if own_skel.parse(bytes):
		# CORRECTED [SOURCED: librw src/hanim.cpp readHAnim(), src/skin.cpp
		# readSkin()] -- the previous version of this code keyed
		# slot_to_bone_name by entry["skin_index"], i.e. the per-node ON-DISK
		# "index" field from the HAnim extension. librw's own readHAnim()
		# reads that exact field and explicitly discards it:
		#     nodeIDs[i] = stream->readI32();
		#     stream->readI32();  // index...unused
		#     nodeFlags[i] = stream->readI32();
		# and HAnimHierarchy::create() sets nodeInfo[i].index = i (the LOOP
		# POSITION), never the value that was just read and thrown away. So
		# that field is not a meaningful bone slot number at all -- at best
		# it coincidentally matched something for player.dff's base skeleton
		# (probably why the leg bones happened to look right), and had no
		# reason to match anything for individual clothing-part files, which
		# is exactly the pattern a real playtest showed: torso/head/hands
		# badly warped, one leg fine.
		#
		# The bone a per-vertex skin slot actually refers to is the ARRAY
		# POSITION in the HAnim node list (0..numNodes-1, i.e. simply the
		# order get_hierarchy_table() returns them in) -- confirmed directly
		# via DragonFF's decoder (see the next comment block below): no
		# further remap needed, the raw per-vertex index already IS that
		# array position.
		var hierarchy: Array = own_skel.get_hierarchy_table()
		for hierarchy_position in hierarchy.size():
			slot_to_bone_name[hierarchy_position] = hierarchy[hierarchy_position]["name"]
	elif verbose:
		print(dff_name, ": no embedded skeleton, falling back to shared table")

	var slot_to_skel_index := {}
	for slot in slot_to_bone_name:
		slot_to_skel_index[slot] = skel_parser.find_bone_by_name(slot_to_bone_name[slot])

	var geo := GTADffGeometry.new()
	if not geo.parse(bytes):
		push_warning("GTACharacterVisual: " + dff_name + " failed to parse geometry")
		return

	# CORRECTED AGAIN [SOURCED: DragonFF (github.com/Parik27/DragonFF), the
	# actual reference DFF importer/exporter, not just an animation tool]:
	# the previous version of this fix added a remap through
	# get_skin_used_bones() before this point, believing it was needed. It
	# wasn't -- confirmed from BOTH directions in DragonFF's gtaLib/dff.py:
	#   - Encode side, SkinPLG.calc_used_bones(): "bones_used" is built by
	#     scanning vertex_bone_indices and collecting the DISTINCT VALUES
	#     ALREADY FOUND there -- i.e. those per-vertex indices are already
	#     full/direct bone indices, not a compact local space.
	#   - Decode side, dff_importer.py's set_vertex_groups(): vertex_groups
	#     is created ONE PER BONE over the full skeleton, and
	#     `bone = skin_data.vertex_bone_indices[i][j]` is used to index it
	#     DIRECTLY -- zero remap through bones_used anywhere.
	# So usedBones/bones_used is just a compact "which bones does this mesh
	# references" summary (useful for e.g. building a small GPU matrix
	# palette on the encode/render side), not something a decoder needs at
	# all. A per-vertex "bone" slot from get_vertex_skin_weights() already
	# IS the hierarchy array position -- used directly, below.

	if verbose:
		_audit_bone_coverage(dff_name, geo, slot_to_bone_name)

	var base_mesh: ArrayMesh = geo.build_array_mesh()
	var arrays: Array = base_mesh.surface_get_arrays(0)
	var vert_count: int = geo.get_vertex_count()

	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	for p in positions:
		if not _found_any_vertex or p.y < _lowest_vertex_y:
			_lowest_vertex_y = p.y
			_found_any_vertex = true

	var bones_arr := PackedInt32Array()
	var weights_arr := PackedFloat32Array()
	bones_arr.resize(vert_count * 4)
	weights_arr.resize(vert_count * 4)

	for v in vert_count:
		var vweights: Array = geo.get_vertex_skin_weights(v)
		var total := 0.0
		for i in 4:
			var hierarchy_position: int = vweights[i]["bone"]
			var w: float = vweights[i]["weight"]
			var skel_idx: int = slot_to_skel_index.get(hierarchy_position, -1)
			if skel_idx == -1:
				skel_idx = 0
				w = 0.0
			bones_arr[v * 4 + i] = skel_idx
			weights_arr[v * 4 + i] = w
			total += w
		if total > 0.0:
			for i in 4:
				weights_arr[v * 4 + i] /= total

	arrays[Mesh.ARRAY_BONES] = bones_arr
	arrays[Mesh.ARRAY_WEIGHTS] = weights_arr

	var skinned_mesh := ArrayMesh.new()
	skinned_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	var mi := MeshInstance3D.new()
	mi.name = dff_name.get_basename()
	mi.mesh = skinned_mesh
	skel.add_child(mi)
	mi.skeleton = mi.get_path_to(skel)

	var txd_name: String = TXD_OVERRIDES.get(dff_name, dff_name.get_basename() + ".txd")
	var txd_bytes: PackedByteArray = img.extract_file(txd_name)
	if not txd_bytes.is_empty():
		var txd := GTATxdTexture.new()
		if txd.parse(txd_bytes) and txd.get_texture_count() > 0:
			var tex: ImageTexture = txd.build_texture_by_index(0)
			if tex:
				var mat := StandardMaterial3D.new()
				mat.albedo_texture = tex
				mi.material_override = mat
				if verbose:
					print(dff_name, ": textured with ", txd.get_texture_names()[0])
			elif verbose:
				print(dff_name, ": texture found but failed to build (unsupported format?)")
		elif verbose:
			print(txd_name, ": no textures / failed to parse")
	elif verbose:
		print(txd_name, ": not found, leaving default material")

	if verbose:
		print(dff_name, ": attached (", vert_count, " verts)")


func _audit_bone_coverage(
	dff_name: String, geo: GTADffGeometry, slot_to_bone_name: Dictionary
) -> void:
	var used_slots := {}
	for v in geo.get_vertex_count():
		for w in geo.get_vertex_skin_weights(v):
			if w["weight"] > 0.0:
				used_slots[w["bone"]] = true
	for slot in used_slots:
		if not slot_to_bone_name.has(slot):
			print(dff_name, ": WARNING hierarchy position ", slot, " is referenced but unmapped!")
