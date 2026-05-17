class_name RWClump
extends RWChunk

var atomic_count: int
var light_count: int
var camera_count: int
var frame_list: RWFrameList
var geometry_list: RWGeometryList
var is_valid := false

func _init(file: FileAccess):
	super(file)
	if type != ChunkType.CLUMP:
		printerr("Error: Expected CLUMP (0x10) at offset %d, got 0x%x" % [_start, type])
		return
	
	# fix
	# a clump chunk always begins with a struct chunk (type 0x01).
	# we must consume this header before reading the counts.
	var struct_type = file.get_32()
	var struct_size = file.get_32()
	var struct_lib_id = file.get_32() # library ID
	
	# verify this is actually a struct
	if struct_type != 0x01:
		printerr("Critical Error: Clump did not start with Struct chunk. Found: 0x%x" % struct_type)
		is_valid = false
		return
		
	atomic_count = file.get_32()
	
	# use the 'struct_lib_id' to check version if needed, 
	# but usually checking the parent Clump version (self.version) is fine 
	if version > 0x33000:
		light_count = file.get_32()
		camera_count = file.get_32()
		
	frame_list = RWFrameList.new(file)
	geometry_list = RWGeometryList.new(file)
	is_valid = true
