extends Node

@export var model_name: String = "carlshou1_lae2" # Example: CJ House
@export var txd_name: String = "contachou1_lae2" # Example: CJ House

var mesh_instance: MeshInstance3D

func _ready() -> void:
	await get_tree().process_frame
	load_object()
	
func load_object() -> void:
	print("Loading model: %s with texture: %s" % [model_name, txd_name])
	
	# load dff
	var dff_file := AssetLoader.open_asset(model_name + ".dff")
	if dff_file == null:
		push_error("No se pudo abrir el archivo: %s.dff" % model_name)
		return
	
	print("dff loaded successfully")
	
	# parser the rw model
	var clump := RWClump.new(dff_file)
	if clump.geometry_list == null or clump.geometry_list.geometries.size() == 0:
		push_error("the model has no geometrys")
		return
		
	print("parsed successfully. found: %d" % clump.geometry_list.geometries.size())
	
	# add mesh instance
	mesh_instance = MeshInstance3D.new()
	add_child(mesh_instance)

	# process each geometry
	for geometry in clump.geometry_list.geometries:
		var mesh := geometry.mesh
		
		if mesh == null:
			push_warning("not a mesh")
			continue
		
		print("created mesh with %d surfaces" % mesh.get_surface_count())
		
		# load texture for each surfaces
		var txd_file := AssetLoader.open_asset(txd_name + ".txd")
		var txd: RWTextureDict = null
		
		if txd_file != null:
			txd = RWTextureDict.new(txd_file)
			print("dictionary of textures loaded: %d textures" % txd.textures.size())
		else:
			push_warning("couldn't load texture: %s.txd" % txd_name)
		
		# apply material and textures
		for surf_id in mesh.get_surface_count():
			var material := mesh.surface_get_material(surf_id) as StandardMaterial3D
			
			if material != null:
				
				material.cull_mode = BaseMaterial3D.CULL_BACK
				material.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
				
				if material.has_meta("texture_name") and txd != null:
					var texture_name = material.get_meta("texture_name")
					print("searching texture: %s" % texture_name)
					
					for raster in txd.textures:
						if texture_name.matchn(raster.name):
							print("found texture!")
							print("  - name: %s" % raster.name)
							print("  - mask_name: %s" % raster.mask_name)
							print("  - platform_id: %d" % raster.platform_id)
							print("  - filter_mode: %d" % raster.filter_mode)
							print("  - u_addressing: %d" % raster.u_addressing)
							print("  - v_addressing: %d" % raster.v_addressing)
							print("  - raster_format: 0x%04x" % raster.raster_format)
							print("  - has_alpha: %s" % raster.has_alpha)
							print("  - width: %d" % raster.width)
							print("  - height: %d" % raster.height)
							print("  - depth: %d" % raster.depth)
							print("  - num_levels: %d" % raster.num_levels)
							print("  - raster_type: %d" % raster.raster_type)
							print("  - compression: %d" % raster.compression)
							
							var img := raster.image
							if img != null and not img.is_empty():
								material.albedo_texture = ImageTexture.create_from_image(img)
								
								
								if raster.has_alpha:
									if texture_name.to_lower().contains("leaf") or \
									   texture_name.to_lower().contains("tree") or \
									   texture_name.to_lower().contains("veg"):
										material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
							else:
								push_warning("the texture %s is empty" % texture_name)
							break
				
				mesh.surface_set_material(surf_id, material)
		
		# assign mesh
		mesh_instance.mesh = mesh
	
	print("model loaded successfully!")
