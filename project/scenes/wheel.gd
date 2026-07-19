extends RayCast3D
class_name Wheel

@export var shapecast : ShapeCast3D
@export var offset_shapecast : float = 0.3

@export_group("Wheel properties")
@export var spring_strength := 100.0
@export var spring_damping := 2.0
@export var max_spring_force : float = INF
@export var rest_dist := 0.5
@export var over_extend := 0.0
@export var wheel_radius := 0.4
@export var z_traction := 0.05
@export var z_brake_traction := 0.25


@export_category("Motor")
@export var is_motor := false
@export var is_steer := false
@export var grip_curve : Curve

@onready var wheel: Node3D = get_child(0)

var engine_force := 0.0
var grip_factor := 0.0
var is_braking := false

func _ready() -> void:
	target_position.y = -(rest_dist + wheel_radius + over_extend)
	
	if shapecast:
		shapecast.target_position.x = -(rest_dist + over_extend) - offset_shapecast
		shapecast.add_exception(get_parent())
		shapecast.position.y = offset_shapecast
	
func apply_wheel_physics(car: Car) -> void:
	target_position.y = -(rest_dist + wheel_radius + over_extend)
	
	if shapecast:
		shapecast.target_position.x = -(rest_dist + over_extend) - offset_shapecast
	
	# rotates wheel visuals
	var forward_dir := -global_basis.z.normalized()
	var speed := forward_dir.dot(car.linear_velocity)
	wheel.rotate_x((-speed * get_physics_process_delta_time()) / wheel_radius)
	
	if not shapecast and not is_colliding(): return
	if shapecast and not shapecast.is_colliding(): return
	# now is colliding
	
	var contact := get_collision_point()
	if shapecast:
		contact = shapecast.get_collision_point(0)
	var spring_len := maxf(0.0, global_position.distance_to(contact) - wheel_radius)
	var offset := rest_dist - spring_len
	
	wheel.position.y = -spring_len # move_toward(wheel.position.y, -spring_len, 5 * get_physics_process_delta_time()) # local y position of the wheel
	contact = wheel.global_position # contact is now the wheel origin point
	var force_pos := contact - car.global_position
	
	# spring forces
	var spring_force := spring_strength * offset
	var tire_vel := car._get_point_velocity(contact)
	var spring_damp_f := spring_damping * global_basis.y.dot(tire_vel)
	var suspension_force := clampf(spring_force - spring_damp_f, -max_spring_force, max_spring_force)
	
	var y_force := suspension_force * get_collision_normal()
	if shapecast:
		y_force = suspension_force * shapecast.get_collision_normal(0)
	
	# acceleration
	if is_motor and car.motor_input:
		var speed_ratio := speed / car.max_speed
		var ac := car.accel_curve.sample_baked(speed_ratio)
		var accel_force := forward_dir * car.acceleration * car.motor_input * ac
		car.apply_force(accel_force, force_pos)
		# debugdraw.draw_arrow(contact, accel_force/car.mass, 2.5, 0.5, color.red)
		
	# tire x traction (steering)
	var steering_x_vel := global_basis.x.dot(tire_vel)
	
	grip_factor = absf(steering_x_vel/tire_vel.length())
	if absf(speed) < 0.2:
		grip_factor = 0
	
	var x_traction := grip_curve.sample_baked(grip_factor)
	
	if not car.hand_break and grip_factor < 0.2:
		car.is_slipping = false
	if car.hand_break:
		x_traction = 0.01
	elif car.is_slipping:
		x_traction = 0.1
		
	var gravity := -car.get_gravity().y
	var x_force := -global_basis.x * steering_x_vel * x_traction * ((car.mass * gravity)/car.total_wheels)
	
	# tire z traction (longidutinasl)
	var f_speed := forward_dir.dot(tire_vel)
	var z_friction := z_traction
	if absf(f_speed) < 0.01:
		z_friction = 2.0
	if is_braking:
		z_friction = z_brake_traction
	var z_force := global_basis.z * f_speed * z_friction * ((car.mass * gravity)/car.total_wheels)
	
	## counter sliding
	if absf(f_speed) < 0.1:
		var susp := global_basis.y * suspension_force
		z_force.z -= susp.z * car.global_basis.y.dot(Vector3.UP)
		x_force.x -= susp.x * car.global_basis.y.dot(Vector3.UP)
		
	car.apply_force(y_force, force_pos)
	car.apply_force(x_force, force_pos)
	car.apply_force(z_force, force_pos)
	
	if shapecast:
		for idx in shapecast.get_collision_count():
			var collider := shapecast.get_collider(idx)
			if collider is RigidBody3D:
				collider.apply_force(-(x_force+y_force+z_force), force_pos)
	
	# debug arrow (contact, y_force/car.mass, 2.5)
	# debug arrow (contact, x_force/car.mass, 1.5, 0.2, color.yellow)
	# debug arrow (contact, x_force/car.mass, 1.5, 0.2, color.yellow)
