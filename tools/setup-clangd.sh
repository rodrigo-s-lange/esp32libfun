#!/usr/bin/env bash
# setup-clangd.sh
# Configures clangd for this ESP32 project by writing machine-specific settings
# to VS Code user settings (not workspace settings — those stay clean for git).
#
# What it does:
#   1. Finds the xtensa-esp-elf toolchain and builds a --query-driver glob.
#   2. Finds the Espressif clangd binary (optional but recommended).
#   3. Writes clangd.path and --query-driver into the VS Code user settings.json.
#
# Run once after cloning (or after updating the toolchain):
#   bash tools/setup-clangd.sh

set -euo pipefail

IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"
XTENSA_BASE="$IDF_TOOLS_PATH/tools/xtensa-esp-elf"

if [ ! -d "$XTENSA_BASE" ]; then
    echo "Error: xtensa-esp-elf not found: $XTENSA_BASE"
    echo "Set IDF_TOOLS_PATH or install ESP-IDF first."
    exit 1
fi

VERSION_DIR=$(ls -1 "$XTENSA_BASE" | sort -rV | head -1)
BIN_DIR="$XTENSA_BASE/$VERSION_DIR/xtensa-esp-elf/bin"
DRIVER_GLOB="$BIN_DIR/xtensa-esp*-elf-*"

# ---- Locate Espressif clangd (optional) ------------------------------------
ESP_CLANG_BIN=""
for candidate in \
    "$IDF_TOOLS_PATH/tools/esp-clang"/*/esp-clang/bin/clangd \
    /opt/esp-clang/bin/clangd; do
    if [ -x "$candidate" ]; then
        ESP_CLANG_BIN="$candidate"
        break
    fi
done

# ---- Detect VS Code user settings path ------------------------------------
if [[ "$OSTYPE" == "darwin"* ]]; then
    USER_SETTINGS="$HOME/Library/Application Support/Code/User/settings.json"
else
    USER_SETTINGS="$HOME/.config/Code/User/settings.json"
fi

mkdir -p "$(dirname "$USER_SETTINGS")"
[ -f "$USER_SETTINGS" ] || echo '{}' > "$USER_SETTINGS"

# ---- Patch user settings with python3 ------------------------------------
python3 - "$USER_SETTINGS" "$DRIVER_GLOB" "$ESP_CLANG_BIN" <<'EOF'
import json, sys

path, driver_glob, clangd_bin = sys.argv[1], sys.argv[2], sys.argv[3]

with open(path) as f:
    s = json.load(f)

managed = ("--query-driver", "--compile-commands-dir", "--background-index")
args = [a for a in s.get("clangd.arguments", []) if a and not any(a.startswith(p) for p in managed)]
args += ["--background-index", "--compile-commands-dir=${workspaceFolder}/build", f"--query-driver={driver_glob}"]
s["clangd.arguments"] = args

if clangd_bin:
    s["clangd.path"] = clangd_bin

with open(path, "w") as f:
    json.dump(s, f, indent=2)
    f.write("\n")
EOF

echo "driver:  $DRIVER_GLOB"
[ -n "$ESP_CLANG_BIN" ] && echo "clangd:  $ESP_CLANG_BIN" || echo "clangd:  (not found — using VS Code extension default)"
echo "Updated: $USER_SETTINGS"
echo ""
echo "Restart the clangd language server in VS Code to apply:"
echo "  Ctrl+Shift+P -> 'clangd: Restart Language Server'"
