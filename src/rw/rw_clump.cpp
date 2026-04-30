#include "rw_clump.h"

void RWClump::parse(Ref<FileAccess> file) {
	read_header(file);

	if (type != RW_CLUMP) {
		ERR_PRINT("Expected CLUMP (0x10) at offset " + String::num_int64(data_start) +
				  ", got 0x" + String::num_int64(type, 16));
		is_valid = false;
		return;
	}

	// A CLUMP always begins with a STRUCT sub-chunk.
	// Read and validate the struct header before reading clump fields.
	uint32_t struct_type   = file->get_32();
	uint32_t struct_size   = file->get_32();
	uint32_t struct_lib_id = file->get_32();
	(void)struct_size;    // Unused but consumed
	(void)struct_lib_id;  // Unused but consumed

	if (struct_type != RW_STRUCT) {
		ERR_PRINT("Clump did not start with Struct chunk. Found: 0x" + String::num_int64(struct_type, 16));
		is_valid = false;
		return;
	}

	atomic_count = file->get_32();

	// Newer versions (> 0x33000) include light and camera counts
	if (get_version() > 0x33000) {
		light_count  = file->get_32();
		camera_count = file->get_32();
	}

	// Parse the frame hierarchy and geometry data
	frame_list.parse(file);
	geometry_list.parse(file);

	is_valid = true;
}
