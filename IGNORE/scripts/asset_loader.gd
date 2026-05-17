extends Node

var assets: Dictionary[String, DirEntry] = {}
var mutex := Mutex.new()

func _ready() -> void:
	load_cd_image("models/gta3.img")

func load_cd_image(path: String) -> void:
	var file := open(path)
	assert(file != null, "Failed to open GTA IMG File: %d" % FileAccess.get_open_error())
	
	# in gta sa, the img file is a different version, the version 2, version 1 works in gta 3 and gta vc
	# i only work on gta sa, maybe i will add for more GTA's
	# learn more: https://gtamods.com/wiki/IMG_archive
	
	# check if is the version 2 of the img file
	var version := file.get_buffer(4).get_string_from_ascii()
	assert(version == "VER2", "Not a valid GTA SA img file (version 2)")
	
	var entry_count := file.get_32()
	
	# read all directory entries
	for i in range(entry_count):
		var entry := DirEntry.new()
		entry.img = path
		entry.offset = int(file.get_32()) * 2048  # offset in sectors
		entry.streaming_size = int(file.get_16()) * 2048  # streaming size in sectors
		entry.archive_size = int(file.get_16()) * 2048  # archive size (usually 0)
		
		# use streaming_size if available, otherwise use archive_size
		entry.size = entry.streaming_size if entry.streaming_size > 0 else entry.archive_size
		
		var name := file.get_buffer(24).get_string_from_ascii().to_lower()
		assets[name] = entry
	
	file.close()
	print("Loaded %d assets from %s" % [entry_count, path])


func open(path: String) -> FileAccess:
	var diraccess := DirAccess.open(GameManager.gta_path)
	var parts := path.replace("\\", "/").split("/")
	for part in parts:
		if part == parts[parts.size() - 1]:
			for file in diraccess.get_files():
				if file.matchn(part):
					return FileAccess.open(diraccess.get_current_dir() + "/" + file, FileAccess.READ)
		else:
			for dir in diraccess.get_directories():
				if dir.matchn(part):
					diraccess.change_dir(dir)
					break
	return null

func open_asset(name: String) -> FileAccess:
	if name.to_lower() in assets:
		var asset = assets[name.to_lower()] as DirEntry
		var access := open(assets[name.to_lower()].img)
		access.seek(asset.offset)
		return access
	return open("models/" + name)

class DirEntry:
	var img: String
	var offset: int
	var size: int
	var streaming_size: int
	var archive_size: int
