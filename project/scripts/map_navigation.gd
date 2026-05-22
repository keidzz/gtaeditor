extends Control


func _on_export_button_pressed() -> void:
	get_tree().change_scene_to_file("res://scenes/export.tscn")
