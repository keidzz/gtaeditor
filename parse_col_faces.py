import struct
with open("project/gta/models/gta3.img", "rb") as f:
    f.seek(24592384) # Offset of levelmap_1.col
    
    for _ in range(50):
        fourcc = f.read(4)
        if not fourcc.startswith(b'COL'):
            break
        size = struct.unpack('<I', f.read(4))[0]
        name = f.read(22).split(b'\0')[0].decode('ascii')
        model_id = struct.unpack('<H', f.read(2))[0]
        
        # Read payload
        payload = f.read(size - 24)
        
        # Read COL2/3 header (spheres, boxes, faces, lines, etc.)
        # GTA SA COL2/COL3 has:
        # radius (4), center (12), min (12), max (12)
        # spheres (2), boxes (2), faces (2), lines (1), pad/flags (1)
        # spheres_offset (4), boxes_offset (4), lines_offset (4), vertices_offset (4), faces_offset (4), shadow_mesh_offset (4)
        num_spheres, num_boxes, num_faces, num_lines, flags = struct.unpack('<HHHHH', payload[40:50])
        
        if num_faces > 0:
            print(f"Found {name} with {num_faces} faces! Size: {size}")
            # Try to unpack offsets if they exist
            if len(payload) >= 72:
                spheres_off, boxes_off, lines_off, verts_off, faces_off, unk_off = struct.unpack('<IIIIII', payload[48:72])
                print(f"  Offsets: Spheres={spheres_off}, Boxes={boxes_off}, Verts={verts_off}, Faces={faces_off}")
            break
