#include "binary_reader.h"

#include <cstring>

// =============================================================================
// BinaryReader implementation
// =============================================================================

BinaryReader::BinaryReader() {}

BinaryReader::BinaryReader(const PackedByteArray &p_data) :
		data(p_data) {
	ptr = data.ptr();
	pos = 0;
	length = data.size();
}

// -- Position control ---------------------------------------------------------

int64_t BinaryReader::get_position() const {
	return pos;
}

void BinaryReader::set_position(int64_t p_pos) {
	pos = p_pos;
}

int64_t BinaryReader::get_length() const {
	return length;
}

void BinaryReader::skip(int32_t p_bytes) {
	pos += p_bytes;
}

bool BinaryReader::can_read(int32_t p_bytes) const {
	return (pos + p_bytes) <= length;
}

// -- Primitive reads ----------------------------------------------------------
// All GTA binary data is little-endian. We use memcpy for safe unaligned reads.

int16_t BinaryReader::read_int16() {
	int16_t val;
	memcpy(&val, ptr + pos, sizeof(val));
	pos += sizeof(val);
	return val;
}

uint16_t BinaryReader::read_uint16() {
	uint16_t val;
	memcpy(&val, ptr + pos, sizeof(val));
	pos += sizeof(val);
	return val;
}

int32_t BinaryReader::read_int32() {
	int32_t val;
	memcpy(&val, ptr + pos, sizeof(val));
	pos += sizeof(val);
	return val;
}

uint32_t BinaryReader::read_uint32() {
	uint32_t val;
	memcpy(&val, ptr + pos, sizeof(val));
	pos += sizeof(val);
	return val;
}

uint64_t BinaryReader::read_uint64() {
	uint64_t val;
	memcpy(&val, ptr + pos, sizeof(val));
	pos += sizeof(val);
	return val;
}

float BinaryReader::read_float() {
	float val;
	memcpy(&val, ptr + pos, sizeof(val));
	pos += sizeof(val);
	return val;
}

// -- Bulk reads ---------------------------------------------------------------

PackedByteArray BinaryReader::read_bytes(int32_t p_count) {
	PackedByteArray result;
	if (p_count <= 0 || !can_read(p_count)) {
		return result;
	}
	result.resize(p_count);
	memcpy(result.ptrw(), ptr + pos, p_count);
	pos += p_count;
	return result;
}

String BinaryReader::read_string(int32_t p_length) {
	// Read a fixed-length buffer and extract the null-terminated string from it.
	if (p_length <= 0 || !can_read(p_length)) {
		return String();
	}
	// Find the null terminator within the buffer.
	const char *start = reinterpret_cast<const char *>(ptr + pos);
	int actual_len = 0;
	while (actual_len < p_length && start[actual_len] != '\0') {
		actual_len++;
	}
	pos += p_length;
	return String::utf8(start, actual_len);
}

// -- RenderWare helpers -------------------------------------------------------

RWSectionHeader BinaryReader::read_section_header() {
	RWSectionHeader header;
	header.type = static_cast<RWSectionType>(read_uint32());
	header.size = read_int32();
	read_uint16(); // Low 16 bits of version (build number), unused
	header.version = static_cast<RWVersion>(read_uint16());
	return header;
}
