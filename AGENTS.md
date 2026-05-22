# Repository Guidelines

## Project Structure & Module Organization

This is a Godot 4 GDExtension project for viewing and exporting the GTA San Andreas map. Native C++ code lives in `src/`: core nodes such as `map_builder.*` and `map_exporter.*` are at the root, RenderWare parsers are in `src/rw/`, and GTA data helpers are in `src/classes/`. Godot project files live in `project/`, with scenes in `project/scenes/`, scripts in `project/scripts/`, prefabs in `project/prefabs/`, and game assets expected under `project/gta/`. Build output is copied into `project/bin/<platform>/`. Generated or ignored reference scripts are under `IGNORE/`.

## Build, Test, and Development Commands

- `scons platform=windows target=template_debug`: builds the Windows debug GDExtension DLL and installs it into `project/bin/windows/`.
- `scons platform=linux target=template_debug`: builds the Linux debug shared library.
- `.\bin\godot\Godot_v4.6.2-stable_win64_console.exe --path project`: opens/runs the Godot project using the bundled Windows Godot binary.

Don't run the godot project, only compile it.

## Coding Style & Naming Conventions

C++ formatting follows `.clang-format`, based on LLVM with tabs disabled by convention in existing files and no enforced column limit. Use Godot C++ bindings idioms: `GDCLASS`, `ClassDB::bind_method`, `Ref<T>`, `Vector<T>`, `HashMap`, and `String`. Class names use PascalCase (`MapExporter`), methods and variables use snake_case (`load_region_ipl`, `export_dir`), and constants/enums use uppercase where already established. Keep comments short and focused on non-obvious parsing, coordinate conversion, or export behavior.

## Configuration Tips

Place GTA SA files under `project/gta/` for the default `res://gta/` path. Keep generated exports outside tracked source folders, for example `Documents/gta_sa_blender_export`. Do not commit proprietary GTA assets, generated Blender exports, or platform build binaries unless intentionally updating release artifacts.
