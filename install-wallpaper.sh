#!/bin/bash
set -e

PLUGIN_ID="com.daniideas.encomglobe"
INSTALL_DIR="$HOME/.local/share/plasma/wallpapers/$PLUGIN_ID"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Instalando Encom Globe wallpaper plugin..."

# Create plugin directories
mkdir -p "$INSTALL_DIR/contents/ui"
mkdir -p "$INSTALL_DIR/contents/web"

# Copy plugin files
cp "$SCRIPT_DIR/plasma-wallpaper/metadata.json"            "$INSTALL_DIR/"
cp "$SCRIPT_DIR/plasma-wallpaper/contents/ui/main.qml"     "$INSTALL_DIR/contents/ui/"
cp "$SCRIPT_DIR/plasma-wallpaper/contents/web/wallpaper.html" "$INSTALL_DIR/contents/web/"

# Copy the web assets
cp "$SCRIPT_DIR/build/encom-globe.js" "$INSTALL_DIR/contents/web/"
cp "$SCRIPT_DIR/grid.js"              "$INSTALL_DIR/contents/web/"
cp "$SCRIPT_DIR/data.js"              "$INSTALL_DIR/contents/web/"

echo ""
echo "Plugin instalado en: $INSTALL_DIR"
echo ""
echo "Para activarlo:"
echo "  1. Clic derecho en el escritorio → Configurar escritorio y fondo de pantalla"
echo "  2. En 'Tipo de fondo de pantalla' selecciona 'Encom Globe'"
echo "  3. Aplicar"
echo ""
echo "Si no aparece, cierra sesión y vuelve a entrar, o ejecuta:"
echo "  plasmashell --replace &"
