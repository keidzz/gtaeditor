extends Control
@onready var export_dir_edit: LineEdit = %ExportDirEdit
@onready var blender_path_edit: LineEdit = %BlenderPathEdit
@onready var progress_bar: ProgressBar = %ProgressBar
@onready var count_label: Label = %CountLabel
@onready var status_label: Label = %StatusLabel
@onready var start_button: Button = %StartButton
@onready var cancel_button: Button = %CancelButton
@onready var regions_box: VBoxContainer = %RegionsBox
@onready var exporter: MapExporter = %MapExporter

func _ready() -> void:
	export_dir_edit.text = OS.get_system_dir(OS.SYSTEM_DIR_DOCUMENTS).path_join("gta_sa_blender_export")
	blender_path_edit.text = "blender"
	_populate_regions()
	_update_progress(0, 0, 0, 0.0, "Choose an export directory and start export.")

func _on_start_button_pressed() -> void:
	start_button.disabled = true
	cancel_button.disabled = false
	exporter.start_export(export_dir_edit.text, "res://gta/", blender_path_edit.text, _get_selected_regions())

func _on_cancel_button_pressed() -> void:
	cancel_button.disabled = true
	exporter.cancel_export()

func _on_back_button_pressed() -> void:
	get_tree().change_scene_to_file("res://scenes/map.tscn")

func _on_browse_export_button_pressed() -> void:
	var dialog = FileDialog.new()
	dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.use_native_dialog = true
	dialog.dir_selected.connect(_on_export_dir_selected)
	dialog.canceled.connect(dialog.queue_free)
	add_child(dialog)
	dialog.popup_centered_ratio(0.55)

func _on_browse_blender_button_pressed() -> void:
	var dialog = FileDialog.new()
	dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.use_native_dialog = true
	dialog.filters = PackedStringArray(["*.exe,*.app,*.bin ; Executables", "* ; All Files"])
	dialog.file_selected.connect(_on_blender_file_selected)
	dialog.canceled.connect(dialog.queue_free)
	add_child(dialog)
	dialog.popup_centered_ratio(0.55)

func _on_export_dir_selected(dir: String) -> void:
	export_dir_edit.text = dir

func _on_blender_file_selected(path: String) -> void:
	blender_path_edit.text = path

func _on_select_all_button_pressed() -> void:
	for child in regions_box.get_children():
		if child is CheckBox:
			child.button_pressed = true

func _on_deselect_all_button_pressed() -> void:
	for child in regions_box.get_children():
		if child is CheckBox:
			child.button_pressed = false

func _on_map_exporter_export_progress(exported: int, remaining: int, total: int, progress: float, status: String) -> void:
	_update_progress(exported, remaining, total, progress, status)

func _on_map_exporter_export_finished(success: bool, message: String) -> void:
	start_button.disabled = false
	cancel_button.disabled = true
	status_label.text = message
	if success:
		progress_bar.value = 100.0

func _update_progress(exported: int, remaining: int, total: int, progress: float, status: String) -> void:
	progress_bar.value = clampf(progress * 100.0, 0.0, 100.0)
	count_label.text = "Exported: %d / %d   Remaining: %d" % [exported, total, remaining]
	status_label.text = status

func _populate_regions() -> void:
	for child in regions_box.get_children():
		child.queue_free()
	var regions: Array = exporter.get_available_regions("res://gta/")
	for region in regions:
		var checkbox := CheckBox.new()
		checkbox.text = str(region)
		checkbox.button_pressed = true
		regions_box.add_child(checkbox)

func _get_selected_regions() -> Array:
	var selected: Array = []
	for child in regions_box.get_children():
		if child is CheckBox and child.button_pressed:
			selected.append(child.text)
	return selected
