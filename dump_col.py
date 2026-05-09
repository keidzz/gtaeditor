import struct
with open("project/gta/models/gta3.img", "rb") as f:
    f.seek(24592384) # Offset of levelmap_1.col
    
    fourcc = f.read(4)
    size = struct.unpack('<I', f.read(4))[0]
    name = f.read(22).split(b'\0')[0].decode('ascii')
    model_id = struct.unpack('<H', f.read(2))[0]
    
    print(f"Header: {fourcc}, size: {size}, name: {name}, id: {model_id}")
    
    radius = struct.unpack('<f', f.read(4))[0]
    center = struct.unpack('<3f', f.read(12))
    bmin = struct.unpack('<3f', f.read(12))
    bmax = struct.unpack('<3f', f.read(12))
    print(f"Bounds: R={radius}, C={center}, Min={bmin}, Max={bmax}")
    
    # Let's read the next 32 bytes to see the counts
    data = f.read(32)
    print("Next bytes:", data.hex())
