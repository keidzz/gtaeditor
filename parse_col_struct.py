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
        if len(payload) > 50:
            num_spheres, num_boxes, num_faces, num_lines, flags = struct.unpack('<HHHHH', payload[40:50])
            if num_faces > 0:
                print(f"Name: {name}, Faces: {num_faces}")
                print(f"Data 40-72 (hex): {payload[40:72].hex()}")
                break
