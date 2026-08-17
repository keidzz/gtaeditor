extends Node3D

## Press F while standing close to the car to hop in and drive it. Press F
## again to hop out next to the car. No enter/exit animations -- the ped is
## simply hidden and control switches to the car, and back.

@export var ped: GTAPedOnFoot
@export var car: CarGD
@export var ped_camera: Camera3D
@export var vehicle_camera: Camera3D
@export var enter_distance := 4.5
@export var exit_local_offset := Vector3(-1.2, 0.0, 0.0)

var in_vehicle := false

var _ped_collision: CollisionShape3D
var _ped_visual: Node3D
var _elapsed := 0.0
var _idle_freeze_delay := 0.75

func _ready() -> void:
	if ped == null:
		push_error("CJVehicleController: 'ped' must be assigned")
		return
	if car == null:
		push_error("CJVehicleController: 'car' must be assigned")
		return
	_ped_collision = ped.get_node_or_null("CollisionShape3D") as CollisionShape3D
	_ped_visual = ped.get_node_or_null("CJVisual") as Node3D
	if ped_camera:
		ped_camera.current = true
	if vehicle_camera:
		vehicle_camera.current = false
	print("[CJVehicleController] ready: car=", car.name, " at ", car.global_position)

func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("enter_vehicle"):
		if in_vehicle:
			exit_vehicle()
		else:
			try_enter_vehicle()

func try_enter_vehicle() -> void:
	if ped == null or car == null:
		return
	var d := ped.global_position.distance_to(car.global_position)
	if d > enter_distance:
		print("[CJVehicleController] too far from car: ", d, " m (need <= ", enter_distance, ")")
		return
	in_vehicle = true
	ped.set_process_mode(Node.PROCESS_MODE_DISABLED)
	if _ped_collision:
		_ped_collision.disabled = true
	if _ped_visual:
		_ped_visual.visible = false
	car.motor_input = 0
	car.set_process_unhandled_input(true)
	car.set_physics_process(true)
	car.freeze = false
	if ped_camera:
		ped_camera.current = false
	if vehicle_camera:
		vehicle_camera.current = true
	print("[CJVehicleController] ENTERED car")

func exit_vehicle() -> void:
	if car == null:
		return
	var exit_pos := car.global_position + car.global_basis * exit_local_offset
	var ground_y := _ground_height_at(exit_pos)
	if is_finite(ground_y):
		exit_pos.y = ground_y + 0.9
	ped.global_position = exit_pos
	ped.rotation.y = car.rotation.y
	ped.velocity = Vector3.ZERO
	ped.set_process_mode(Node.PROCESS_MODE_INHERIT)
	if _ped_collision:
		_ped_collision.disabled = false
	if _ped_visual:
		_ped_visual.visible = true
	_park_car()
	if ped_camera:
		ped_camera.current = true
	if vehicle_camera:
		vehicle_camera.current = false
	in_vehicle = false
	print("[CJVehicleController] EXITED car")

## Puts the car to sleep: freezes its body and disables its script so the
## wheels can't apply forces from the shared W/S/A/D input while the ped
## walks around.
func _park_car() -> void:
	car.motor_input = 0
	car.set_process_unhandled_input(false)
	car.set_physics_process(false)
	car.freeze = true

func _physics_process(delta: float) -> void:
	if in_vehicle or car == null:
		return
	if car.freeze:
		return
	_elapsed += delta
	if _elapsed >= _idle_freeze_delay:
		_park_car()
		print("[CJVehicleController] car parked (frozen, script disabled)")

## Ground height under pos, or NAN if no ground is found within the probe.
func _ground_height_at(pos: Vector3) -> float:
	var space := get_world_3d().direct_space_state
	if space == null:
		return NAN
	var params := PhysicsRayQueryParameters3D.create(pos + Vector3.UP * 2.0, pos + Vector3.DOWN * 4.0)
	if car:
		params.exclude = [car.get_rid()]
	var hit := space.intersect_ray(params)
	if hit.is_empty():
		return NAN
	return (hit["position"] as Vector3).y
