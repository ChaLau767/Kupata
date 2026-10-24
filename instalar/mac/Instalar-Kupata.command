#!/bin/bash
# Instala Kupata en esta Mac, solo para el usuario actual.
# Copia el plugin (VST3, AU y CLAP) a la carpeta de plugins y la aplicación a ~/Aplicaciones.

set -e
cd "$(dirname "$0")"

PLUGINS="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$PLUGINS/VST3" "$PLUGINS/Components" "$PLUGINS/CLAP" "$HOME/Applications"

instalar () {
    local origen="$1" destino="$2"
    if [ ! -e "$origen" ]; then
        echo "No encuentro $origen junto a este instalador. ¿Descomprimiste el paquete completo?"
        exit 1
    fi
    rm -rf "$destino/$origen"
    cp -R "$origen" "$destino/"
    # quita la marca de "descargado de internet" para que macOS deje cargarlo
    xattr -dr com.apple.quarantine "$destino/$origen" 2>/dev/null || true
    echo "  ✓ $origen  →  $destino"
}

echo ""
echo "Instalando Kupata…"
instalar "Kupata.vst3"      "$PLUGINS/VST3"
instalar "Kupata.component" "$PLUGINS/Components"
instalar "Kupata.clap"      "$PLUGINS/CLAP"
instalar "Kupata.app"       "$HOME/Applications"

# hace que macOS vuelva a leer la lista de Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Listo. En Reaper: Preferencias → Plug-ins → VST → «Re-scan»."
echo "La aplicación independiente quedó en tu carpeta Aplicaciones personal (~/Applications)."
echo ""
