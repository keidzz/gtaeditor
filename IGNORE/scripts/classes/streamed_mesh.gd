class_name StreamedMesh
extends MeshInstance3D

var _idef: ItemDef
var _thread := Thread.new()
var _mesh_buf: Mesh

func _init(idef: ItemDef):
	_idef = idef

func _exit_tree():
	if _thread.is_alive():
		_thread.wait_to_finish()

func _process(delta: float) -> void:
	if _thread.is_started() == false:
		if get_viewport().get_camera_3d() != null:
			var dist := get_viewport().get_camera_3d().global_position.distance_to(global_position)
			if dist < visibility_range_end and mesh == null:
				_thread.start(_load_mesh)
				while _thread.is_alive():
					await get_tree().process_frame
				_thread.wait_to_finish()
				mesh = _mesh_buf
			elif dist > visibility_range_end and mesh != null:
				mesh = null

func _load_mesh() -> void:
	AssetLoader.mutex.lock()
	
	if _idef.flags & 0x40:
		AssetLoader.mutex.unlock()
		return
		
	var access := AssetLoader.open_asset(_idef.model_name + ".dff")
	
	if access == null:
		AssetLoader.mutex.unlock()
		return
		
	var clump := RWClump.new(access)
	if not clump.is_valid:
		printerr("Failed to load Clump for: ", _idef.model_name)
		AssetLoader.mutex.unlock()
		return
		
	var glist := clump.geometry_list
	for geometry in glist.geometries:
		_mesh_buf = geometry.mesh
		for surf_id in _mesh_buf.get_surface_count():
			var material := _mesh_buf.surface_get_material(surf_id) as StandardMaterial3D
			
			material.cull_mode = BaseMaterial3D.CULL_DISABLED
			var is_material_transparent = material.albedo_color.a < 0.95

			if _idef.flags & 0x08:
				material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
				material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
				# additive materials generally shouldn't write depth
				material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
				
			if material.has_meta("texture_name"):
				var txd := RWTextureDict.new(AssetLoader.open_asset(_idef.txd_name + ".txd"))
				var texture_name = material.get_meta("texture_name")
				for raster in txd.textures:
					if texture_name.matchn(raster.name):
						if raster.image != null and not raster.image.is_empty():
							var img := raster.image
							material.albedo_texture = ImageTexture.create_from_image(img)
							material.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
							material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_OPAQUE_ONLY
							
							if is_material_transparent:
								material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
								material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_ALWAYS
								
							else:
								# we scan the image because raster.has_alpha is unreliable
								var alpha_mode := img.detect_alpha()
								
								if alpha_mode != Image.ALPHA_NONE:
									# scissor casts shadows
									material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
									material.alpha_scissor_threshold = 0.5
						else:
							push_warning("Empty image for: %s" % texture_name)
						break
			_mesh_buf.surface_set_material(surf_id, material)
	AssetLoader.mutex.unlock()
