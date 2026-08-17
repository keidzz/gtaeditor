extends Node3D
## Minimal orbit camera for testing GTAPedOnFoot.
##
## This is NOT a port of gta-reversed's TheCamera — that's explicitly a later
## step in the plan. This just gives the player controller something to be
## camera-relative to, so Step A is actually testable/visible.
##
## Deliberately decoupled from the ped's own rotation: this node follows the
## ped's *position* every physics frame, but its own yaw/pitch are driven
## independently by the mouse, same relationship as TheCamera.m_fOrientation
## in the source (the ped turns to face camera-relative input; the camera
## does not inherit the ped's facing).

@export var target_path: NodePath
@export var mouse_sensitivity: float = 0.005
@export var pitch_min: float = deg_to_rad(-60.0)
@export var pitch_max: float = deg_to_rad(70.0)
@export var follow_height: float = 1.6 # roughly shoulder height on CJ; re-check in-editor

var target: Node3D
var yaw: float = 0.0
var pitch: float = deg_to_rad(-15.0)

func _ready() -> void:
	if not target_path.is_empty():
		target = get_node_or_null(target_path)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		yaw -= event.relative.x * mouse_sensitivity
		pitch -= event.relative.y * mouse_sensitivity
		pitch = clamp(pitch, pitch_min, pitch_max)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		else:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _physics_process(_delta: float) -> void:
	if target:
		global_position = target.global_position + Vector3.UP * follow_height
	rotation = Vector3(pitch, yaw, 0.0)
