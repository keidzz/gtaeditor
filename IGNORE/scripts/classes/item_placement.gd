class_name ItemPlacement
extends RefCounted

var id: int
var model_name: String
var interior: int
var position: Vector3
var scale: Vector3
var rotation: Quaternion
var lod_index: int = -1  # -1 means no LOD, otherwise index to parent object
