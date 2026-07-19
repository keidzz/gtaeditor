extends RigidBody3D
class_name Car

@export var wheels: Array[Wheel]
@export var acceleration := 600.0 
@export var max_speed := 20.0
@export var accel_curve : Curve
@export var tire_turn_speed := 2.0
@export var tire_max_turn_degrees := 25

@export var skid_marks: Array[GPUParticles3D]


@onready var total_wheels := wheels.size()

var motor_input := 0
var hand_break := false
var is_slipping := false


func _get_point_velocity(point: Vector3) -> Vector3:
	return linear_velocity + angular_velocity.cross(point - to_global(center_of_mass))

func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("handbreak"):
		hand_break = true
		is_slipping = true
	elif event.is_action_released("handbreak"):
		hand_break = false

	if event.is_action_pressed("accelerate"):
		motor_input = 1
	elif event.is_action_released("accelerate"):
		motor_input = 0

	if event.is_action_pressed("decelerate"):
		motor_input = -1
	elif event.is_action_released("decelerate"):
		motor_input = 0
		
func _basic_steering_rotation(wheel: Wheel, delta: float) -> void:
	if not wheel.is_steer: return
	
	var turn_input := Input.get_axis("turn_right", "turn_left") * tire_turn_speed
	
	if turn_input:
		wheel.rotation.y = clampf(wheel.rotation.y + turn_input * delta,
			deg_to_rad(-tire_max_turn_degrees), deg_to_rad(tire_max_turn_degrees))
	else:
		wheel.rotation.y = move_toward(wheel.rotation.y, 0, tire_turn_speed * delta)
				


func _physics_process(delta: float) -> void:
	# debugdraw arrow global_position, linear_velocity, 2.5, 0.5, color.yellow
	var id := 0
	var grounded := false
	
	for wheel in wheels:
		wheel.apply_wheel_physics(self)
		_basic_steering_rotation(wheel, delta)
		
		if Input.is_action_pressed("brake"):
			wheel.is_braking = true
		else:
			wheel.is_braking = false
	
		# skid marks
		skid_marks[id].global_position = wheel.get_collision_point() + Vector3.UP * 0.01
		skid_marks[id].look_at(skid_marks[id].global_position + global_basis.z)	
			
		if not hand_break and wheel.grip_factor < 0.2:
			is_slipping = false
			skid_marks[id].emitting = false
		
		if hand_break and not skid_marks[id].emitting:
			skid_marks[id].emitting = true
			
		if wheel.is_colliding():
			grounded = true
		
		id += 1
