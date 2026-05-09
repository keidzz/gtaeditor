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
        payload = f.read(size - 24)
        num_spheres, num_boxes, num_faces, num_lines = struct.unpack('<HHHH', payload[40:48])
        if num_faces > 0:
            print(f"Data 48-72: {payload[48:72].hex()}")
            print(f"Spheres_off: {struct.unpack('<I', payload[48:52])[0]}")
            print(f"Boxes_off: {struct.unpack('<I', payload[52:56])[0]}")
            print(f"Lines_off: {struct.unpack('<I', payload[56:60])[0]}")
            print(f"Verts_off: {struct.unpack('<I', payload[60:64])[0]}")
            print(f"Faces_off: {struct.unpack('<I', payload[64:68])[0]}")
            break
