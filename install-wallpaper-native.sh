#!/bin/bash
set -e

PLUGIN_ID="com.daniideas.encomglobe.native"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_DIR="$HOME/.local/share/plasma/wallpapers/$PLUGIN_ID"

"$SCRIPT_DIR/native/build.sh"

echo "Instalando fondo de pantalla en $INSTALL_DIR"
mkdir -p "$INSTALL_DIR/contents/ui" "$INSTALL_DIR/contents/encomglobe"
cp "$SCRIPT_DIR/plasma-wallpaper-native/metadata.json"        "$INSTALL_DIR/"
cp "$SCRIPT_DIR/plasma-wallpaper-native/contents/ui/main.qml" "$INSTALL_DIR/contents/ui/"
# El plugin va dentro del paquete; main.qml lo importa como "../encomglobe",
# así plasmashell lo encuentra sin QML_IMPORT_PATH.
cp "$SCRIPT_DIR/native/build/qml/org/daniideas/encomglobe/"* "$INSTALL_DIR/contents/encomglobe/"

# Versiones anteriores de este script instalaban el módulo en ~/.local/lib/qt6/qml y
# exportaban QML_IMPORT_PATH al iniciar sesión; ya no hace falta.
rm -f "$HOME/.config/plasma-workspace/env/encomglobe-qml.sh"
rm -rf "$HOME/.local/lib/qt6/qml/org/daniideas/encomglobe"

echo ""
echo "Listo. Para activarlo:"
echo "  1. Reinicia Plasma:  plasmashell --replace &   (o cierra sesión y vuelve a entrar)"
echo "  2. Clic derecho en el escritorio → Configurar escritorio y fondo de pantalla"
echo "  3. En 'Tipo de fondo de pantalla' selecciona 'Encom Globe (Nativo)'"
