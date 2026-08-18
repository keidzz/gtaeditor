extends Control

# CJ's (Johnson) house interior spawn. Converted from GTA SA coords
# (2496.0549, -1695.1749, 1014.7422): Godot = (gta_x, gta_z, -gta_y).
const CJ_HOUSE_POS := Vector3(2496.0549, 1014.7422, 1695.1749)


func _on_export_button_pressed() -> void:
	get_tree().change_scene_to_file("res://scenes/export.tscn")


func _on_cj_house_button_pressed() -> void:
	var ped := get_node_or_null("../CJ") as CharacterBody3D
	if ped == null:
		return
	var target := CJ_HOUSE_POS
	var ground_y := _ground_height_at(target)
	if is_finite(ground_y):
		target.y = ground_y + 0.9
	var controller := get_node_or_null("../CJController")
	var car := get_node_or_null("../Car") as RigidBody3D
	if controller != null and controller.get("in_vehicle") == true and car != null:
		car.global_position = target
		car.linear_velocity = Vector3.ZERO
		car.angular_velocity = Vector3.ZERO
	else:
		ped.global_position = target
		ped.velocity = Vector3.ZERO


## Ground height under pos, or NAN if no ground is found within the probe.
func _ground_height_at(pos: Vector3) -> float:
	var space := get_viewport().get_world_3d().direct_space_state
	if space == null:
		return NAN
	var params := PhysicsRayQueryParameters3D.create(pos + Vector3.UP * 2.0, pos + Vector3.DOWN * 4.0)
	var hit := space.intersect_ray(params)
	if hit.is_empty():
		return NAN
	return (hit["position"] as Vector3).y
