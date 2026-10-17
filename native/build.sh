#!/bin/bash
# Builds the native globe QML module into native/build/qml/org/daniideas/encomglobe
set -e
cd "$(dirname "$0")"

QTBIN="$(qmake6 -query QT_HOST_BINS)"
mkdir -p generated build

node tools/grid2bin.js ../grid.js generated/grid.bin
node tools/grid2bin.js ../grid-particles.js generated/grid-particles.bin
node tools/data2json.js ../data.js generated/data.json
for shader in shaders/*.vert shaders/*.frag; do
    "$QTBIN/qsb" --glsl "100 es,120,150" --hlsl 50 --msl 12 -o "generated/$(basename "$shader").qsb" "$shader"
done

(cd build && qmake6 ../encomglobe.pro && make -j"$(nproc)")

MODULE_DIR=build/qml/org/daniideas/encomglobe
mkdir -p "$MODULE_DIR"
cp build/libencomglobeplugin.so module/qmldir "$MODULE_DIR/"
echo "Módulo listo en native/$MODULE_DIR"
