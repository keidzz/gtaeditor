/*
    GTAPedOnFoot — Step A: bare on-foot movement state machine + physics, no animation.

    This is a from-scratch GDExtension port that mirrors the STRUCTURE and the
    SOURCED CONSTANTS of gta-reversed's player-on-foot code, not a transliteration
    of its code (that code is Win32/RenderWare-coupled and can't run outside an
    injected GTA:SA process — see the provenance table in the .cpp).

    Primary reference (all addresses are gta-reversed's, i.e. offsets into the
    GTA:SA "Compact" 1.0 US exe that gta-reversed's hooks are installed over):
      - source/game_sa/Tasks/TaskTypes/TaskSimplePlayerOnFoot.cpp
            CTaskSimplePlayerOnFoot::PlayerControlZelda   (0x6883D0)
      - source/game_sa/Tasks/TaskTypes/TaskSimpleJump.cpp
            CTaskSimpleJump::Launch                       (0x679B80)
      - source/game_sa/Entity/Physical.cpp
            CPhysical::ApplyGravity                       (0x5429??, see .cpp)
            CPhysical::ApplyMoveForce                      (0x5429F0)
      - source/game_sa/Entity/Ped/Ped.cpp
            CPed::CPed ctor (m_fMass = 70.0f)              (line 239)
      - source/game_sa/Timer.h
            CTimer::TIMESTEP_PER_SECOND = 50.f
      - source/game_sa/Tasks/TaskTypes/TaskSimpleInAir.cpp
            CTaskSimpleInAir::ProcessPed                  (0x680600)
      - source/game_sa/Tasks/TaskTypes/TaskComplexInAirAndLand.cpp
            CTaskComplexInAirAndLand::CreateNextSubTask   (0x67CCB0)
      - source/game_sa/Tasks/TaskTypes/TaskSimpleGetUp.cpp
            CTaskSimpleGetUp::StartAnim                   (0x67C770)

    Every exported tunable below is commented as either SOURCED (with the exact
    provenance) or PLACEHOLDER (not found in gta-reversed — see the research
    notes in the .cpp for exactly why, and what would resolve it).
*/
#ifndef GTA_PED_ON_FOOT_H
#define GTA_PED_ON_FOOT_H

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace godot {

class CollisionShape3D;

class GTAPedOnFoot : public CharacterBody3D {
	GDCLASS(GTAPedOnFoot, CharacterBody3D)

public:
	// Mirrors gta-reversed's eMoveState exactly (Enums/eMoveState.h), including
	// PEDMOVE_JOG/TURN_L/TURN_R which this Step A controller doesn't drive yet,
	// so the enum stays wire-compatible with later steps and with GDScript/anim
	// state-machine code that wants to switch on it.
	enum PedMoveState {
		PEDMOVE_NONE = 0,
		PEDMOVE_STILL,
		PEDMOVE_TURN_L,
		PEDMOVE_TURN_R,
		PEDMOVE_WALK,
		PEDMOVE_JOG,
		PEDMOVE_RUN,
		PEDMOVE_SPRINT,
	};

private:
	// ---- SOURCED tunables (see .cpp for the derivation of each) ----
	real_t move_blend_accel = 3.5f;
	real_t gravity = 20.0f;
	real_t jump_vertical_speed = 6.0714285f;
	real_t jump_horizontal_speed_walk = 5.0f;
	real_t jump_horizontal_speed_run_min = 5.0f;
	real_t jump_horizontal_speed_run_max = 8.5f;
	real_t jump_horizontal_speed_sprint_min = 8.5f;
	real_t jump_horizontal_speed_sprint_max = 11.0f;

	// Fall thresholds, per-frame game values x50 (TIMESTEP_PER_SECOND):
	//   fall_flail_speed_threshold  -0.1F per frame  (TaskSimpleInAir.cpp:147)
	//   fall_ko_speed_threshold     -0.4F per frame  (TaskComplexInAirAndLand.cpp:63)
	//   fall_free_fall_height         4.0F world units (TaskSimpleInAir.cpp:56)
	//   fall_ko_down_time             700ms           (TaskComplexInAirAndLand.cpp:64)
	real_t fall_flail_speed_threshold = 5.0f;
	real_t fall_ko_speed_threshold = 20.0f;
	real_t fall_free_fall_height = 4.0f;
	real_t fall_ko_down_time = 0.7f;

	// ---- PLACEHOLDER tunables (NOT found in gta-reversed; see .cpp notes) ----
	real_t walk_speed = 1.5f;
	real_t run_speed = 4.0f;
	real_t sprint_speed = 7.0f;
	real_t turn_rate = 12.0f; // rad/s
	// Max curb/riser height the ped can walk up without jumping (see .cpp).
	real_t step_height = 0.3f;

	// ---- Behaviour toggles ----
	bool sprint_is_toggle = false;
	bool walk_is_toggle = false;
	bool sprint_disabled = false; // mirrors CPlayerPedData::m_bPlayerSprintDisabled

	// ---- Camera reference for camera-relative input, GTA's classic feel ----
	NodePath camera_path;
	Node3D *camera_node = nullptr;

	// ---- Runtime state (read-only from GDScript; see getters) ----
	PedMoveState move_state = PEDMOVE_NONE;
	real_t move_blend_ratio = 0.0f; // 0..1, smoothed input intensity
	real_t facing_angle = 0.0f;     // current visual heading (radians, Y-up)
	real_t aiming_rotation = 0.0f;  // desired heading from input+camera (radians)
	bool is_in_the_air = false;
	bool sprint_toggle_state = false;
	bool walk_toggle_state = false;
	bool prev_jump_pressed = false;
	bool prev_sprint_pressed = false;
	bool prev_walk_pressed = false;

	// ---- Fall runtime state (mirrors the InAir/Fall/GetUp task trio) ----
	bool fall_flail_active = false;          // FALL_FALL arms-out flail is playing
	real_t min_fall_speed = 0.0f;            // most negative vel.y of this airtime
	bool fall_control_locked = false;        // input frozen while down/getting up
	real_t fall_down_time_remaining = 0.0f;  // KO down-time countdown (seconds)

	// Floor snap length to restore after a jump. Snapping is disabled for the
	// duration of the airtime (see _apply_jump) so the launch isn't cancelled.
	real_t floor_snap_length_restore = 0.1f;

	// Cached CollisionShape3D child used by the step-up probes (see .cpp).
	CollisionShape3D *collision_shape_node = nullptr;

	// ---- Input action names (also added to project.godot's [input] map) ----
	StringName action_move_forward = StringName("gta_move_forward");
	StringName action_move_back = StringName("gta_move_back");
	StringName action_move_left = StringName("gta_move_left");
	StringName action_move_right = StringName("gta_move_right");
	StringName action_sprint = StringName("gta_sprint");
	StringName action_walk = StringName("gta_walk");
	StringName action_jump = StringName("gta_jump");

	Vector2 _read_move_axis() const;
	Vector3 _camera_relative_direction(const Vector2 &axis) const;
	void _update_move_state(real_t target_blend_ratio, bool sprint_held, bool walk_held);
	void _step_move_blend_ratio(real_t target_blend_ratio, double delta);
	real_t _current_target_speed() const;
	void _apply_jump();
	void _turn_towards(real_t target_angle, double delta);
	bool _is_free_fall() const;
	void _cache_collision_shape();
	bool _try_step_up(const Vector3 &p_dir, real_t p_dist);

protected:
	static void _bind_methods();

public:
	GTAPedOnFoot();
	~GTAPedOnFoot() override;

	void _ready() override;
	void _physics_process(double delta) override;

	// --- Exported property accessors ---
	void set_move_blend_accel(real_t p_value);
	real_t get_move_blend_accel() const;

	void set_gravity(real_t p_value);
	real_t get_gravity() const;

	void set_jump_vertical_speed(real_t p_value);
	real_t get_jump_vertical_speed() const;

	void set_walk_speed(real_t p_value);
	real_t get_walk_speed() const;

	void set_run_speed(real_t p_value);
	real_t get_run_speed() const;

	void set_sprint_speed(real_t p_value);
	real_t get_sprint_speed() const;

	void set_turn_rate(real_t p_value);
	real_t get_turn_rate() const;

	void set_step_height(real_t p_value);
	real_t get_step_height() const;

	void set_sprint_is_toggle(bool p_value);
	bool get_sprint_is_toggle() const;

	void set_walk_is_toggle(bool p_value);
	bool get_walk_is_toggle() const;

	void set_sprint_disabled(bool p_value);
	bool get_sprint_disabled() const;

	void set_fall_flail_speed_threshold(real_t p_value);
	real_t get_fall_flail_speed_threshold() const;

	void set_fall_ko_speed_threshold(real_t p_value);
	real_t get_fall_ko_speed_threshold() const;

	void set_fall_free_fall_height(real_t p_value);
	real_t get_fall_free_fall_height() const;

	void set_fall_ko_down_time(real_t p_value);
	real_t get_fall_ko_down_time() const;

	void set_camera_path(const NodePath &p_path);
	NodePath get_camera_path() const;

	// --- Read-only runtime state, for GDScript/anim state machines to poll ---
	PedMoveState get_move_state() const;
	real_t get_move_blend_ratio() const;
	real_t get_facing_angle() const;
	bool get_is_in_the_air() const;

	// Called by the visual when the get-up (hard fall) or collapse (soft
	// fall) animation has finished playing -- ends the input lock. Mirrors
	// CTaskSimpleGetUp::FinishGetUpAnimCB (0x678110) letting the move task
	// resume.
	void notify_fall_recovered();
};

} // namespace godot

VARIANT_ENUM_CAST(godot::GTAPedOnFoot::PedMoveState);

#endif // GTA_PED_ON_FOOT_H
