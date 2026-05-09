extends SceneTree
func _init():
    var file = FileAccess.open("project/gta/models/gta3.img", FileAccess.READ)
    var d = file.get_buffer(4)
    if d.get_string_from_ascii() == "VER2":
        var count = file.get_32()
        for i in range(count):
            file.seek(8 + i * 32)
            var offset = file.get_32() * 2048
            var size = file.get_16() * 2048
            file.get_16()
            var name = file.get_buffer(24).get_string_from_ascii()
            if name.to_lower() == "lae2.col" or name.to_lower() == "veh_mods.col":
                print("Found ", name, " at ", offset)
                file.seek(offset)
                var col_data = file.get_buffer(4)
                print("Magic: ", col_data.get_string_from_ascii())
    quit()
