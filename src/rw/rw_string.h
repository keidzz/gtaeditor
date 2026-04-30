#ifndef GTAEDITOR_RW_STRING_H
#define GTAEDITOR_RW_STRING_H

#include "rw_chunk.h"

/// Reads a null-terminated ASCII string from an RW STRING chunk.
class RWString : public RWChunk {
public:
	String value;

	/// Parse an RW STRING chunk from the current file position.
	void parse(Ref<FileAccess> file);
};

#endif // GTAEDITOR_RW_STRING_H
