#ifndef GTAEDITOR_RW_TEXTURE_H
#define GTAEDITOR_RW_TEXTURE_H

#include "rw_chunk.h"

/// RW Texture chunk: references a texture by name within a texture dictionary.
/// Contains the diffuse texture name and an optional alpha mask name.
class RWTexture : public RWChunk {
public:
	String texture_name;
	String mask_name;

	/// Parse a TEXTURE chunk from the current file position.
	void parse(Ref<FileAccess> file);
};

#endif // GTAEDITOR_RW_TEXTURE_H
