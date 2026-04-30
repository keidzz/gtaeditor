#include "rw_string.h"

void RWString::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_STRING, "Expected STRING chunk.");

	// Read bytes until null terminator, then skip to chunk end.
	PackedByteArray chars;
	chars.resize(size);
	chars = file->get_buffer(size);

	// Find null terminator and extract string
	int len = 0;
	const uint8_t *ptr = chars.ptr();
	while (len < (int)size && ptr[len] != 0) {
		len++;
	}
	value = String::utf8((const char *)ptr, len);

	// Ensure we're at the chunk boundary
	skip(file);
}
