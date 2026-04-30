#!/bin/bash

# parse args
NOBUILD=0
for arg in "$@"; do
    if [[ "${arg,,}" == "nobuild" ]]; then
        NOBUILD=1
    fi
done

# SET PATHS HERE!!
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GODOT="/home/keithu/Documentos/projects/gtaeditor/bin/godot/Godot_v4.6.2-stable_linux.x86_64"
PROJECT="$ROOT/project"

# verify if godot exists
if [ ! -f "$GODOT" ]; then
    echo "ERROR: Not found godot executable at $GODOT"
    echo "Please place the godot binary there."
    read -p "Press Enter to continue..."
    exit 1
fi

# compile if its on building mode
if [ "$NOBUILD" -eq 0 ]; then
    echo "Compiling extension..."
    cd "$ROOT"
    scons platform=linux target=template_debug
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to compile"
        read -p "Press Enter to continue..."
        exit 1
    fi
fi

# open godot
echo "Opening project in godot..."
"$GODOT" --path "$PROJECT" --editor
