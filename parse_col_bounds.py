import struct
data = bytes.fromhex("8a226ec2f0f3a7c2164a97c1962f6e4294faa742169d974100b0d03b0060d43b0000a63c1068cf42000000000000000000000000000000000000000000000000000000000000000000000000")
radius = struct.unpack('<f', data[0:4])[0]
center = struct.unpack('<3f', data[4:16])
bmin = struct.unpack('<3f', data[16:28])
bmax = struct.unpack('<3f', data[28:40])
print(f"Bounds: R={radius}, C={center}, Min={bmin}, Max={bmax}")
print(f"Remaining 36 bytes: {data[40:].hex()}")

num_spheres, num_boxes, num_faces, num_lines, flags = struct.unpack('<HHHHH', data[40:50])
print(f"Spheres: {num_spheres}, Boxes: {num_boxes}, Faces: {num_faces}, Lines: {num_lines}, Flags: {flags}")
