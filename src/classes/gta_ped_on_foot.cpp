/*
    GTAPedOnFoot implementation notes / research provenance
    ========================================================

    This is Step A only: bare movement state machine + physics, no animation,
    per the agreed incremental plan. Every constant below is one of:

      [SOURCED]      — taken directly from gta-reversed (github.com/gta-reversed/gta-reversed),
                        with file/function/address noted so it can be verified against
                        the actual repo.
      [DERIVED]       — computed FROM sourced constants (e.g. dividing a sourced force
                        by a sourced mass), shown with the arithmetic so it's checkable.
      [PLACEHOLDER]   — NOT found in gta-reversed. Explained below. Exported so you can
                        tune by feel until it's replaced with something sourced.

    --------------------------------------------------------------------------
    [SOURCED] Move-blend-ratio acceleration clamp
    --------------------------------------------------------------------------
    CTaskSimplePlayerOnFoot::PlayerControlZelda (source/game_sa/Tasks/TaskTypes/
    TaskSimplePlayerOnFoot.cpp, 0x6883D0) clamps how fast the smoothed move-blend
    ratio can approach its target:

        float fMaximumMoveBlendRatio = CTimer::GetTimeStep() * 0.07f;

    CTimer::GetTimeStep() is not seconds — per Timer.h, ms_fTimeStep is scaled so
    that 1.0 == one frame at the reference rate of TIMESTEP_PER_SECOND = 50.f.
    So in real seconds: fMaximumMoveBlendRatio/sec = 0.07 * 50 = 3.5.
    -> move_blend_accel = 3.5f (units of blend-ratio per second)

    --------------------------------------------------------------------------
    [SOURCED] Gravity
    --------------------------------------------------------------------------
    CPhysical::ApplyGravity (source/game_sa/Entity/Physical.cpp):

        m_vecMoveSpeed.z -= CTimer::GetTimeStep() * 0.008f;

    Same timestep normalization as above: 0.008 * 50 * 50 = 20.0 (the timestep
    scaling applies twice here because this is added to velocity every timestep,
    i.e. it's an acceleration, so it picks up a factor of TIMESTEP_PER_SECOND
    twice when converting to units/sec^2).
    -> gravity = 20.0f (world-units/sec^2, downward)

    --------------------------------------------------------------------------
    [SOURCED]+[DERIVED] Jump vertical speed
    --------------------------------------------------------------------------
    CTaskSimpleJump::Launch (source/game_sa/Tasks/TaskTypes/TaskSimpleJump.cpp,
    0x679B80):

        float fJumpForce = (ped->IsPlayer() || m_bHighJump) ? 8.5F : 4.5F;
        ped->ApplyMoveForce(0.0F, 0.0F, fJumpForce);

    CPhysical::ApplyMoveForce (source/game_sa/Entity/Physical.cpp, 0x5429F0):

        m_vecMoveSpeed += force / m_fMass;

    CPed::CPed ctor (source/game_sa/Entity/Ped/Ped.cpp, line 239): m_fMass = 70.0f.

    m_vecMoveSpeed is itself in units/timestep, so the instantaneous vertical
    velocity kick in units/sec is (8.5 / 70.0) * 50 = 6.0714285...
    -> jump_vertical_speed = 6.0714285f

    (Sanity check against the sourced gravity above: v^2/(2g) = 6.0714^2/40 ~= 0.92
    world-units of jump height. Plausible for CJ's jump, but I haven't cross-checked
    this against footage — flag if it feels wrong once you're testing against your
    own game install.)

    --------------------------------------------------------------------------
    [SOURCED] Jump horizontal speed
    --------------------------------------------------------------------------
    Same function, a few lines up:

        float fHorizontalJumpSpeed = 0.1F;
        if (sprint anim playing) fHorizontalJumpSpeed = lerp(0.17F, 0.22F, sprintBlend);
        else if (run anim playing) fHorizontalJumpSpeed = lerp(0.1F, 0.17F, runBlend);

    This is set directly on m_vecMoveSpeed.x/y (NOT run through ApplyMoveForce, so
    NOT divided by mass) — these are already "final" units/timestep values.
    Converting to units/sec by the same *50 factor:
        walk-ish jump:        0.10 * 50 = 5.0
        run range:      0.10..0.17 * 50 = 5.0..8.5
        sprint range:    0.17..0.22 * 50 = 8.5..11.0
    -> jump_horizontal_speed_walk = 5.0f
    -> jump_horizontal_speed_run_min/max = 5.0f / 8.5f
    -> jump_horizontal_speed_sprint_min/max = 8.5f / 11.0f

    The original keys this off which animation is *currently blended in* and by
    how much (RpAnimBlendClumpGetAssociation(..., ANIM_ID_SPRINT)->m_BlendAmount).
    Step A has no animations, so this port substitutes move_state + move_blend_ratio
    as the closest available signal — same shape (lerp across a range based on how
    "committed" you are to run/sprint), different underlying variable. Revisit once
    Step C wires in real anim blend amounts.

    --------------------------------------------------------------------------
    [PLACEHOLDER] Walk / run / sprint horizontal speed magnitudes
    --------------------------------------------------------------------------
    I went looking for this specifically and could not source it. Here's exactly
    why, so it's not just "I didn't look hard enough":

    CPed::SetMoveAnimSpeed (source/game_sa/Entity/Ped/Ped.cpp, 0x5DEC10) only sets
    *animation playback rate* (association->m_Speed = slopePitch + 1.2 - perPedRandom*0.4),
    not ped translation speed. The actual ped position is driven by the animation's
    own root/bone motion each frame, extracted inside the RenderWare anim-blend
    update — and the function that does that per-frame update,
    RpAnimBlendClumpUpdateAnimations (0x4D34F0, source/game_sa/Plugins/
    RpAnimBlendPlugin/RpAnimBlend.cpp), is explicitly marked in gta-reversed itself
    as NOT reversed:

        RH_ScopedGlobalInstall(RpAnimBlendClumpUpdateAnimations, 0x4D34F0,
            {.reversed = false}); // TODO: Hook this again... Unhooked for testing.

    So there's no "GTA:SA walk speed = X" constant sitting in a table anywhere in
    this codebase — speed is an emergent property of whichever animation clip is
    playing, which this project doesn't have loaded yet (that's Step C). These
    three numbers are therefore genuinely placeholder, picked to feel roughly
    human-scaled (assuming 1 world-unit ~= 1 meter, itself a common but unverified-
    in-source modding-community assumption) rather than measured from anything.
    Treat them as "tune by feel until Step C, then replace with whatever the real
    anim's root motion produces."
    -> walk_speed = 1.5f, run_speed = 4.0f, sprint_speed = 7.0f

    --------------------------------------------------------------------------
    [PLACEHOLDER] Turn rate
    --------------------------------------------------------------------------
    PlayerControlZelda sets player->m_fAimingRotation directly to the camera-
    relative input angle every frame — it's the *target* facing. What actually
    interpolates the ped's visible heading toward that target (and by how much
    per frame) lives outside what I read this pass; PEDMOVE_TURN_L/TURN_R and the
    ANIM_ID_TURN_L/TURN_R animations in CPed::SetMoveAnim suggest turning-in-place
    is itself an animation-driven state for large angle deltas while stationary,
    not a simple rotate-toward-target lerp. Kept as a flat rad/sec turn rate here
    as a placeholder; worth a dedicated follow-up research pass before Step B/D.
    -> turn_rate = 12.0f (rad/s)

    --------------------------------------------------------------------------
    NOT implemented in Step A (intentionally, to avoid guessing):
      - Stamina-gated sprint (CPed::ControlButtonSprint(SPRINT_GROUND) >= 1.0f
        in the source gates sprint on a depletable stat; sprint_disabled here is
        just a manual on/off switch, no depletion model).
      - The source's check that RUN and SPRINT resolve to the identical animation
        hierarchy for some ped models, which disables sprint entirely for them.
      - Jump being blocked by nearby geometry (CTaskSimpleJump::CheckIfJumpBlocked)
        — Step A relies on CharacterBody3D collision only.
    --------------------------------------------------------------------------

    --------------------------------------------------------------------------
    [SOURCED] Fall detection and landing response
    --------------------------------------------------------------------------
    This is the TaskSimpleInAir -> TaskComplexInAirAndLand trio from
    source/game_sa/Tasks/TaskTypes/. All per-frame speeds are the game's
    m_vecMoveSpeed (units per 1/50s timestep), converted to units/sec by the
    *50 TIMESTEP_PER_SECOND factor used everywhere above.

    CTaskSimpleInAir::ProcessPed (TaskSimpleInAir.cpp @ 0x680600):
      - m_fMinZSpeed = std::min(m_vecMoveSpeed.z, m_fMinZSpeed)   (line 125)
      - Free-fall test: CWorld::ProcessVerticalLine(posn, posn.z - 4.0F)
        (lines 56/129) -- only when there is NO ground within 4 world units
        below does the ped count as freely falling.
      - The FALL_FALL arms-out flail starts when
            m_vecMoveSpeed.z < -0.1F                              (line 147)
        i.e. a descent faster than 5.0 units/sec. A normal jump's descent
        (~6 units/sec at touchdown) never reaches this while the ground is
        within 4 units, so ordinary jumps do NOT flail.

    CTaskComplexInAirAndLand::CreateNextSubTask (TaskComplexInAirAndLand.cpp
    @ 0x67CCB0), on touchdown while the FALL_FALL flail is playing:
      - m_fMinZSpeed < -0.4F (=-20 units/sec) -> new CTaskSimpleFall(
        ANIM_ID_KO_SKID_BACK, ANIM_GROUP_DEFAULT, 700)          (lines 63-64)
        -- the KO skid plays for 700ms of down-time, then a
        CTaskSimpleGetUp takes over (lines 46-47).
      - otherwise -> new CTaskSimpleLand(ANIM_ID_FALL_COLLAPSE)  (line 66)
        -- the collapse plays once, then the move task resumes.
      - without the flail the ped lands on its feet: player gets
        JUMP_LAND when moving (blend ratio past half + pad input) else
        FALL_LAND (lines 78-90) -- the visual picks the clip off the plain
        "landed" signal.

    CTaskSimpleGetUp::StartAnim (TaskSimpleGetUp.cpp @ 0x67C770) plays
    ANIM_ID_GETUP_FRONT if a front anim is associated, else ANIM_ID_GETUP_0
    ("getup" -- the back get-up for our KO_SKID_BACK falls), and finishes via
    FinishGetUpAnimCB (@ 0x678110), after which the move task resumes. The
    visual mirrors this: hard fall -> KO_skid_back for the 700ms down time,
    then "getup"; its finish calls notify_fall_recovered() to release the
    input lock. Soft fall -> FALL_collapse; same release on its finish.

    The 700ms down time is counted in real time here (delta seconds), same as
    the game's CTimer::GetTimeStepInMS-based countdown in
    CTaskSimpleFall::ProcessPed (TaskSimpleFall.cpp @ 0x67FAF0).

    NOT implemented:
      - The high-fall scream (lines 58-64) and the collapse/land sound events
        (AE_PED_COLLAPSE_AFTER_FALL / AE_PED_LAND_ON_FEET_AFTER_FALL) -- no
        audio system in this project yet.
      - The FALL_glide slow-fall branch (lines 76-84, 131-143) -- that is the
        separate TaskComplexFall "walked off a ledge" presentation; falls
        here either flail (and KO/collapse) or land on the feet, which is the
        correct behavior for all cases the current physics can produce.
      - Fall damage: CPed::ProcessControl (0x5E8CD0), the state machine that
        ENTERS PEDSTATE_FALL (53, Enums/ePedState.h) and creates the
        CEventDamage(WEAPON_FALL) response, is itself not reversed in
        gta-reversed (plugin::CallMethod stub), so there is no sourced
        damage formula to port. Deferred until the ped has a health system.
    --------------------------------------------------------------------------
*/

#include "gta_ped_on_foot.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

GTAPedOnFoot::GTAPedOnFoot() {
}

GTAPedOnFoot::~GTAPedOnFoot() {
}

void GTAPedOnFoot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_move_blend_accel", "value"), &GTAPedOnFoot::set_move_blend_accel);
	ClassDB::bind_method(D_METHOD("get_move_blend_accel"), &GTAPedOnFoot::get_move_blend_accel);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "move_blend_accel"), "set_move_blend_accel", "get_move_blend_accel");

	ClassDB::bind_method(D_METHOD("set_gravity", "value"), &GTAPedOnFoot::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &GTAPedOnFoot::get_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity"), "set_gravity", "get_gravity");

	ClassDB::bind_method(D_METHOD("set_jump_vertical_speed", "value"), &GTAPedOnFoot::set_jump_vertical_speed);
	ClassDB::bind_method(D_METHOD("get_jump_vertical_speed"), &GTAPedOnFoot::get_jump_vertical_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_vertical_speed"), "set_jump_vertical_speed", "get_jump_vertical_speed");

	ClassDB::bind_method(D_METHOD("set_walk_speed", "value"), &GTAPedOnFoot::set_walk_speed);
	ClassDB::bind_method(D_METHOD("get_walk_speed"), &GTAPedOnFoot::get_walk_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walk_speed"), "set_walk_speed", "get_walk_speed");

	ClassDB::bind_method(D_METHOD("set_run_speed", "value"), &GTAPedOnFoot::set_run_speed);
	ClassDB::bind_method(D_METHOD("get_run_speed"), &GTAPedOnFoot::get_run_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "run_speed"), "set_run_speed", "get_run_speed");

	ClassDB::bind_method(D_METHOD("set_sprint_speed", "value"), &GTAPedOnFoot::set_sprint_speed);
	ClassDB::bind_method(D_METHOD("get_sprint_speed"), &GTAPedOnFoot::get_sprint_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sprint_speed"), "set_sprint_speed", "get_sprint_speed");

	ClassDB::bind_method(D_METHOD("set_turn_rate", "value"), &GTAPedOnFoot::set_turn_rate);
	ClassDB::bind_method(D_METHOD("get_turn_rate"), &GTAPedOnFoot::get_turn_rate);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turn_rate"), "set_turn_rate", "get_turn_rate");

	ClassDB::bind_method(D_METHOD("set_sprint_is_toggle", "value"), &GTAPedOnFoot::set_sprint_is_toggle);
	ClassDB::bind_method(D_METHOD("get_sprint_is_toggle"), &GTAPedOnFoot::get_sprint_is_toggle);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sprint_is_toggle"), "set_sprint_is_toggle", "get_sprint_is_toggle");

	ClassDB::bind_method(D_METHOD("set_walk_is_toggle", "value"), &GTAPedOnFoot::set_walk_is_toggle);
	ClassDB::bind_method(D_METHOD("get_walk_is_toggle"), &GTAPedOnFoot::get_walk_is_toggle);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "walk_is_toggle"), "set_walk_is_toggle", "get_walk_is_toggle");

	ClassDB::bind_method(D_METHOD("set_sprint_disabled", "value"), &GTAPedOnFoot::set_sprint_disabled);
	ClassDB::bind_method(D_METHOD("get_sprint_disabled"), &GTAPedOnFoot::get_sprint_disabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sprint_disabled"), "set_sprint_disabled", "get_sprint_disabled");

	ClassDB::bind_method(D_METHOD("set_camera_path", "path"), &GTAPedOnFoot::set_camera_path);
	ClassDB::bind_method(D_METHOD("get_camera_path"), &GTAPedOnFoot::get_camera_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_camera_path", "get_camera_path");

	ClassDB::bind_method(D_METHOD("set_fall_flail_speed_threshold", "value"), &GTAPedOnFoot::set_fall_flail_speed_threshold);
	ClassDB::bind_method(D_METHOD("get_fall_flail_speed_threshold"), &GTAPedOnFoot::get_fall_flail_speed_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fall_flail_speed_threshold"), "set_fall_flail_speed_threshold", "get_fall_flail_speed_threshold");

	ClassDB::bind_method(D_METHOD("set_fall_ko_speed_threshold", "value"), &GTAPedOnFoot::set_fall_ko_speed_threshold);
	ClassDB::bind_method(D_METHOD("get_fall_ko_speed_threshold"), &GTAPedOnFoot::get_fall_ko_speed_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fall_ko_speed_threshold"), "set_fall_ko_speed_threshold", "get_fall_ko_speed_threshold");

	ClassDB::bind_method(D_METHOD("set_fall_free_fall_height", "value"), &GTAPedOnFoot::set_fall_free_fall_height);
	ClassDB::bind_method(D_METHOD("get_fall_free_fall_height"), &GTAPedOnFoot::get_fall_free_fall_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fall_free_fall_height"), "set_fall_free_fall_height", "get_fall_free_fall_height");

	ClassDB::bind_method(D_METHOD("set_fall_ko_down_time", "value"), &GTAPedOnFoot::set_fall_ko_down_time);
	ClassDB::bind_method(D_METHOD("get_fall_ko_down_time"), &GTAPedOnFoot::get_fall_ko_down_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fall_ko_down_time"), "set_fall_ko_down_time", "get_fall_ko_down_time");

	ClassDB::bind_method(D_METHOD("get_move_state"), &GTAPedOnFoot::get_move_state);
	ClassDB::bind_method(D_METHOD("get_move_blend_ratio"), &GTAPedOnFoot::get_move_blend_ratio);
	ClassDB::bind_method(D_METHOD("get_facing_angle"), &GTAPedOnFoot::get_facing_angle);
	ClassDB::bind_method(D_METHOD("get_is_in_the_air"), &GTAPedOnFoot::get_is_in_the_air);

	ClassDB::bind_method(D_METHOD("notify_fall_recovered"), &GTAPedOnFoot::notify_fall_recovered);

	ADD_SIGNAL(MethodInfo("move_state_changed", PropertyInfo(Variant::INT, "new_state"), PropertyInfo(Variant::INT, "old_state")));
	ADD_SIGNAL(MethodInfo("jumped"));
	ADD_SIGNAL(MethodInfo("landed"));
	ADD_SIGNAL(MethodInfo("fall_started", PropertyInfo(Variant::FLOAT, "fall_speed")));
	ADD_SIGNAL(MethodInfo("fall_landed_hard", PropertyInfo(Variant::FLOAT, "min_speed")));
	ADD_SIGNAL(MethodInfo("fall_landed_soft", PropertyInfo(Variant::FLOAT, "min_speed")));
	ADD_SIGNAL(MethodInfo("fall_getup_started"));

	BIND_ENUM_CONSTANT(PEDMOVE_NONE);
	BIND_ENUM_CONSTANT(PEDMOVE_STILL);
	BIND_ENUM_CONSTANT(PEDMOVE_TURN_L);
	BIND_ENUM_CONSTANT(PEDMOVE_TURN_R);
	BIND_ENUM_CONSTANT(PEDMOVE_WALK);
	BIND_ENUM_CONSTANT(PEDMOVE_JOG);
	BIND_ENUM_CONSTANT(PEDMOVE_RUN);
	BIND_ENUM_CONSTANT(PEDMOVE_SPRINT);
}

void GTAPedOnFoot::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (!camera_path.is_empty()) {
		camera_node = Object::cast_to<Node3D>(get_node_or_null(camera_path));
	}
}

// Reads WASD-equivalent actions as raw digital axes, deliberately NOT normalized
// diagonally (a Vector2(1,1) stays length sqrt(2)) — this mirrors PlayerControlZelda's
// pedMoveBlendRatio = sqrt(x*x + y*y), which is likewise un-normalized for keyboard-
// style digital input in the source. Only walk-toggle clamps the ratio to 1.0, same
// as the source's `if (pad->NewState.m_bPedWalk && pedMoveBlendRatio > 1.0f) ratio = 1.0f;`.
Vector2 GTAPedOnFoot::_read_move_axis() const {
	Input *input = Input::get_singleton();
	real_t x = 0.0f;
	real_t y = 0.0f;
	if (input->is_action_pressed(action_move_right)) {
		x += 1.0f;
	}
	if (input->is_action_pressed(action_move_left)) {
		x -= 1.0f;
	}
	if (input->is_action_pressed(action_move_forward)) {
		y += 1.0f;
	}
	if (input->is_action_pressed(action_move_back)) {
		y -= 1.0f;
	}
	return Vector2(x, y);
}

// Camera-relative direction, GTA's classic third-person-action feel: input is
// expressed relative to the camera's flattened forward/right, not the ped's own
// facing (source equivalent: TheCamera.m_fOrientation). Without a camera node
// (headless tests) it falls back to fixed world axes -- the old fallback of the
// ped's own basis made the aiming chase itself and spin in a circle.
Vector3 GTAPedOnFoot::_camera_relative_direction(const Vector2 &axis) const {
	if (axis.length_squared() < 0.0001f) {
		return Vector3();
	}

	Vector3 cam_forward = Vector3(0.0f, 0.0f, 1.0f);
	Vector3 cam_right = Vector3(1.0f, 0.0f, 0.0f);

	if (camera_node != nullptr) {
		cam_forward = -camera_node->get_global_transform().basis.get_column(2);
		cam_right = camera_node->get_global_transform().basis.get_column(0);
	}

	cam_forward.y = 0.0f;
	cam_right.y = 0.0f;
	if (cam_forward.length_squared() > 0.0001f) {
		cam_forward = cam_forward.normalized();
	}
	if (cam_right.length_squared() > 0.0001f) {
		cam_right = cam_right.normalized();
	}

	return (cam_forward * axis.y) + (cam_right * axis.x);
}

// [SOURCED shape, see header comment] default RUN unless walk-toggled (-> WALK)
// or sprint held+eligible (-> SPRINT); STILL when there's no input at all.
void GTAPedOnFoot::_update_move_state(real_t target_blend_ratio, bool sprint_held, bool walk_held) {
	PedMoveState old_state = move_state;

	if (target_blend_ratio <= 0.0001f) {
		move_state = PEDMOVE_STILL;
	} else if (walk_held) {
		move_state = PEDMOVE_WALK;
	} else if (sprint_held && !sprint_disabled) {
		move_state = PEDMOVE_SPRINT;
	} else {
		move_state = PEDMOVE_RUN;
	}

	if (move_state != old_state) {
		emit_signal("move_state_changed", (int)move_state, (int)old_state);
	}
}

// [SOURCED] rate-limited approach to the target ratio, 3.5/sec either direction —
// see header comment for the CTimer::GetTimeStep()*0.07f -> 3.5/sec derivation.
void GTAPedOnFoot::_step_move_blend_ratio(real_t target_blend_ratio, double delta) {
	real_t max_step = move_blend_accel * (real_t)delta;
	real_t diff = target_blend_ratio - move_blend_ratio;
	if (diff > max_step) {
		move_blend_ratio += max_step;
	} else if (diff < -max_step) {
		move_blend_ratio -= max_step;
	} else {
		move_blend_ratio = target_blend_ratio;
	}
}

// [PLACEHOLDER magnitudes, see header] current target horizontal speed given
// move_state and how "into" the current input the smoothed blend ratio is.
real_t GTAPedOnFoot::_current_target_speed() const {
	real_t ratio = Math::clamp(move_blend_ratio, (real_t)0.0, (real_t)1.0);
	switch (move_state) {
		case PEDMOVE_WALK:
			return walk_speed * ratio;
		case PEDMOVE_RUN:
			return run_speed * ratio;
		case PEDMOVE_SPRINT:
			return sprint_speed * ratio;
		default:
			return 0.0f;
	}
}

// [SOURCED constants, substituted signal — see header comment] jump impulse.
void GTAPedOnFoot::_apply_jump() {
	Vector3 vel = get_velocity();
	vel.y = jump_vertical_speed;

	// [SOURCED] CTaskSimpleJump::Launch (0x679B80): the horizontal jump
	// speed defaults to 0.1F/frame (5 units/sec) and the game SETS the
	// horizontal velocity to it in the facing direction even from a
	// standstill (lines 146-148 -- the idle jump hops forward), raised to
	// lerp(0.1F, 0.17F, runBlend) while running and lerp(0.17F, 0.22F,
	// sprintBlend) while sprinting (lines 119-128).
	real_t horizontal_speed;
	real_t ratio = Math::clamp(move_blend_ratio, (real_t)0.0, (real_t)1.0);
	switch (move_state) {
		case PEDMOVE_SPRINT:
			horizontal_speed = Math::lerp(jump_horizontal_speed_sprint_min, jump_horizontal_speed_sprint_max, ratio);
			break;
		case PEDMOVE_RUN:
			horizontal_speed = Math::lerp(jump_horizontal_speed_run_min, jump_horizontal_speed_run_max, ratio);
			break;
		default:
			horizontal_speed = jump_horizontal_speed_walk;
			break;
	}

	Vector3 facing_dir = Vector3(Math::sin(facing_angle), 0.0f, Math::cos(facing_angle));
	vel.x = facing_dir.x * horizontal_speed;
	vel.z = facing_dir.z * horizontal_speed;

	set_velocity(vel);
	is_in_the_air = true;
	emit_signal("jumped");
}

// [PLACEHOLDER rate, see header] rotate facing_angle towards aiming_rotation,
// shortest-path, at a flat rad/sec rate.
void GTAPedOnFoot::_turn_towards(real_t target_angle, double delta) {
	real_t diff = Math::wrapf(target_angle - facing_angle, (real_t)-Math::PI, (real_t)Math::PI);
	real_t max_step = turn_rate * (real_t)delta;
	if (Math::abs(diff) <= max_step) {
		facing_angle = target_angle;
	} else {
		facing_angle += (diff > 0.0f ? max_step : -max_step);
	}
	facing_angle = Math::wrapf(facing_angle, (real_t)-Math::PI, (real_t)Math::PI);
}

void GTAPedOnFoot::_physics_process(double delta) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// [SOURCED] KO down-time (TaskComplexInAirAndLand.cpp:64, 700ms) runs in
	// real time before the get-up task starts. While locked the ped ignores
	// ALL input, same as the game's PEDSTATE_FALL with the move task inactive.
	if (fall_control_locked) {
		if (fall_down_time_remaining > 0.0f) {
			fall_down_time_remaining -= (real_t)delta;
			if (fall_down_time_remaining <= 0.0f) {
				emit_signal("fall_getup_started");
			}
		}
		return;
	}

	Input *input = Input::get_singleton();

	// --- Sprint / walk hold-or-toggle handling ---
	bool sprint_pressed = input->is_action_pressed(action_sprint);
	bool walk_pressed = input->is_action_pressed(action_walk);

	bool sprint_held;
	if (sprint_is_toggle) {
		if (sprint_pressed && !prev_sprint_pressed) {
			sprint_toggle_state = !sprint_toggle_state;
		}
		sprint_held = sprint_toggle_state;
	} else {
		sprint_held = sprint_pressed;
	}

	bool walk_held;
	if (walk_is_toggle) {
		if (walk_pressed && !prev_walk_pressed) {
			walk_toggle_state = !walk_toggle_state;
		}
		walk_held = walk_toggle_state;
	} else {
		walk_held = walk_pressed;
	}
	prev_sprint_pressed = sprint_pressed;
	prev_walk_pressed = walk_pressed;

	// --- Movement intent ---
	Vector2 axis = _read_move_axis();
	real_t target_blend_ratio = axis.length();
	if (walk_held && target_blend_ratio > 1.0f) {
		target_blend_ratio = 1.0f; // [SOURCED clamp, see PlayerControlZelda excerpt in header]
	}

	_step_move_blend_ratio(target_blend_ratio, delta);
	_update_move_state(target_blend_ratio, sprint_held, walk_held);

	Vector3 move_dir = _camera_relative_direction(axis);
	if (move_dir.length_squared() > 0.0001f && !is_in_the_air) {
		// [SOURCED] the facing is frozen while airborne: TaskSimpleInAir
		// (0x680600) never rotates the ped, so the launch direction is kept
		// for the whole jump.
		move_dir = move_dir.normalized();
		aiming_rotation = Math::atan2((double)move_dir.x, (double)move_dir.z);
		_turn_towards(aiming_rotation, delta);
	}

	// --- Horizontal velocity from state + blend ratio (floor only) ---
	// [SOURCED] The air coast: TaskSimpleInAir::ProcessPed applies NO input
	// or friction while airborne -- CTaskSimpleJump::Launch set the
	// horizontal velocity at takeoff and it stays frozen (the only air force
	// is a forward nudge toward 0.05F/frame = 2.5 units/sec, lines 113-119,
	// which never engages for player jumps since Launch always sets >=
	// 0.1F/frame = 5 units/sec). Releasing input mid-jump does nothing.
	Vector3 vel = get_velocity();
	if (!is_in_the_air) {
		real_t speed = _current_target_speed();
		vel.x = move_dir.x * speed;
		vel.z = move_dir.z * speed;
	}

	// --- Gravity ---
	bool was_on_floor = is_on_floor();
	if (!was_on_floor) {
		vel.y -= gravity * (real_t)delta;
	} else if (vel.y < 0.0f) {
		vel.y = 0.0f;
	}
	set_velocity(vel);

	// --- Jump (edge-triggered, matches pad->JumpJustDown() semantics) ---
	bool jump_pressed = input->is_action_pressed(action_jump);
	bool jump_just_pressed = jump_pressed && !prev_jump_pressed;
	prev_jump_pressed = jump_pressed;

	if (jump_just_pressed && was_on_floor && !is_in_the_air) {
		_apply_jump();
	}

	// [SOURCED] Fall tracking (TaskSimpleInAir.cpp @ 0x680600):
	//   - m_fMinZSpeed = min(z, m_fMinZSpeed) (line 125) -- the most negative
	//     vertical speed of this airtime, decisive at touchdown.
	//   - The FALL_FALL flail (line 147: z < -0.1F per frame, i.e. faster
	//     than -5 units/sec) only starts while there is NO ground within 4
	//     units below (the ProcessVerticalLine test, lines 56/129), so
	//     normal jumps and short drops never flail.
	if (is_in_the_air) {
		min_fall_speed = Math::min(min_fall_speed, vel.y);
		if (!fall_flail_active && vel.y < -fall_flail_speed_threshold && _is_free_fall()) {
			fall_flail_active = true;
			emit_signal("fall_started", vel.y);
		}
	}

	move_and_slide();

	bool now_on_floor = is_on_floor();
	if (now_on_floor && is_in_the_air) {
		is_in_the_air = false;
		if (fall_flail_active) {
			// [SOURCED] TaskComplexInAirAndLand.cpp:60-66: a flail landing
			// with min fall speed < -0.4F per frame (=-20 units/sec) becomes
			// CTaskSimpleFall(KO_SKID_BACK, ANIM_GROUP_DEFAULT, 700) --
			// KO skid for the 700ms down time, then the get-up. Anything
			// slower becomes CTaskSimpleLand(FALL_COLLAPSE). The horizontal
			// velocity is dropped so the KO/collapse plays in place (the
			// game's own skid/collapse root motion is not applied here).
			Vector3 landed_vel = get_velocity();
			landed_vel.x = 0.0f;
			landed_vel.z = 0.0f;
			set_velocity(landed_vel);
			if (min_fall_speed < -fall_ko_speed_threshold) {
				fall_control_locked = true;
				fall_down_time_remaining = fall_ko_down_time;
				emit_signal("fall_landed_hard", min_fall_speed);
			} else {
				fall_control_locked = true;
				emit_signal("fall_landed_soft", min_fall_speed);
			}
		} else {
			// [PLACEHOLDER] the game keeps the horizontal momentum on a
			// feet landing (its landing anim's root motion matches it), but
			// our landing anims are root-stripped (static), so keeping the
			// momentum looks like gliding -- drop it for static landings.
			Vector3 landed_vel = get_velocity();
			landed_vel.x = 0.0f;
			landed_vel.z = 0.0f;
			set_velocity(landed_vel);
			emit_signal("landed");
		}
		fall_flail_active = false;
		min_fall_speed = 0.0f;
	} else if (!now_on_floor && !is_in_the_air) {
		is_in_the_air = true;
		min_fall_speed = 0.0f;
	}

	// Apply the visual heading. Kept separate from movement direction (which can
	// differ briefly during turns) same as the source's split between
	// m_fCurrentRotation (visual) and m_fAimingRotation (target).
	Vector3 rot = get_rotation();
	rot.y = facing_angle;
	set_rotation(rot);
}

// --- Property accessors ---

void GTAPedOnFoot::set_move_blend_accel(real_t p_value) { move_blend_accel = p_value; }
real_t GTAPedOnFoot::get_move_blend_accel() const { return move_blend_accel; }

void GTAPedOnFoot::set_gravity(real_t p_value) { gravity = p_value; }
real_t GTAPedOnFoot::get_gravity() const { return gravity; }

void GTAPedOnFoot::set_jump_vertical_speed(real_t p_value) { jump_vertical_speed = p_value; }
real_t GTAPedOnFoot::get_jump_vertical_speed() const { return jump_vertical_speed; }

void GTAPedOnFoot::set_walk_speed(real_t p_value) { walk_speed = p_value; }
real_t GTAPedOnFoot::get_walk_speed() const { return walk_speed; }

void GTAPedOnFoot::set_run_speed(real_t p_value) { run_speed = p_value; }
real_t GTAPedOnFoot::get_run_speed() const { return run_speed; }

void GTAPedOnFoot::set_sprint_speed(real_t p_value) { sprint_speed = p_value; }
real_t GTAPedOnFoot::get_sprint_speed() const { return sprint_speed; }

void GTAPedOnFoot::set_turn_rate(real_t p_value) { turn_rate = p_value; }
real_t GTAPedOnFoot::get_turn_rate() const { return turn_rate; }

void GTAPedOnFoot::set_sprint_is_toggle(bool p_value) { sprint_is_toggle = p_value; }
bool GTAPedOnFoot::get_sprint_is_toggle() const { return sprint_is_toggle; }

void GTAPedOnFoot::set_walk_is_toggle(bool p_value) { walk_is_toggle = p_value; }
bool GTAPedOnFoot::get_walk_is_toggle() const { return walk_is_toggle; }

void GTAPedOnFoot::set_sprint_disabled(bool p_value) { sprint_disabled = p_value; }
bool GTAPedOnFoot::get_sprint_disabled() const { return sprint_disabled; }

void GTAPedOnFoot::set_camera_path(const NodePath &p_path) {
	camera_path = p_path;
	if (is_inside_tree() && !camera_path.is_empty()) {
		camera_node = Object::cast_to<Node3D>(get_node_or_null(camera_path));
	}
}
NodePath GTAPedOnFoot::get_camera_path() const { return camera_path; }

void GTAPedOnFoot::set_fall_flail_speed_threshold(real_t p_value) { fall_flail_speed_threshold = p_value; }
real_t GTAPedOnFoot::get_fall_flail_speed_threshold() const { return fall_flail_speed_threshold; }

void GTAPedOnFoot::set_fall_ko_speed_threshold(real_t p_value) { fall_ko_speed_threshold = p_value; }
real_t GTAPedOnFoot::get_fall_ko_speed_threshold() const { return fall_ko_speed_threshold; }

void GTAPedOnFoot::set_fall_free_fall_height(real_t p_value) { fall_free_fall_height = p_value; }
real_t GTAPedOnFoot::get_fall_free_fall_height() const { return fall_free_fall_height; }

void GTAPedOnFoot::set_fall_ko_down_time(real_t p_value) { fall_ko_down_time = p_value; }
real_t GTAPedOnFoot::get_fall_ko_down_time() const { return fall_ko_down_time; }

// [SOURCED proxy] TaskSimpleInAir::ProcessPed (0x680600) tests
// CWorld::ProcessVerticalLine(posn, posn.z - 4.0F) to decide whether the ped
// is in a true free fall (no ground within 4 world units below) versus a
// short drop the engine lets him land on his feet. Godot has no per-ped
// vertical line probe, so this is a straight-down raycast of the same
// length from the ped's origin (which sits a bit above the game's
// feet-level ped origin, so the effective free-fall boundary is ~4m + the
// body offset -- same shape, slightly conservative). The +0.2m margin keeps
// the boundary from flickering right at 4m.
bool GTAPedOnFoot::_is_free_fall() const {
	PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
	if (space == nullptr) {
		return false;
	}
	Ref<PhysicsRayQueryParameters3D> params;
	params.instantiate();
	params->set_from(get_global_position());
	params->set_to(get_global_position() - Vector3(0.0f, fall_free_fall_height + 0.2f, 0.0f));
	Array exclude;
	exclude.append(get_rid());
	params->set_exclude(exclude);
	return space->intersect_ray(params).is_empty();
}

// Called by the visual once the get-up (hard fall) or collapse (soft fall)
// animation has finished. [SOURCED shape] CTaskSimpleGetUp::FinishGetUpAnimCB
// (0x678110) ends the get-up task the same way, letting the move task resume.
void GTAPedOnFoot::notify_fall_recovered() {
	fall_control_locked = false;
	fall_down_time_remaining = 0.0f;
}

GTAPedOnFoot::PedMoveState GTAPedOnFoot::get_move_state() const { return move_state; }
real_t GTAPedOnFoot::get_move_blend_ratio() const { return move_blend_ratio; }
real_t GTAPedOnFoot::get_facing_angle() const { return facing_angle; }
bool GTAPedOnFoot::get_is_in_the_air() const { return is_in_the_air; }
