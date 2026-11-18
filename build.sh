#!/usr/bin/env bash
# Compila o firmware Radar360 PV2KSM.
# Requer arduino-cli, o core esp32 3.3.12 e as bibliotecas: LovyanGFX 1.2.33, ArduinoJson 7.4.2,
# AsyncTCP 3.5.0 e ESPAsyncWebServer 3.12.1.
# Variaveis opcionais (para instalacao manual do toolchain): ESP_X32, ESP_LIBS, ESPTOOL_DIR
set -euo pipefail
cd "$(dirname "$0")"

python3 tools/build_web.py

# FLASH=qio (padrao desde a v0.16.0, leitura da flash ~2x mais rapida) ou FLASH=dio (reserva, se algum aparelho nao iniciar em QIO)
FLASH=${FLASH:-qio}
SUF=""; [ "$FLASH" = qio ] || SUF="-$FLASH"
FQBN="espressif:esp32:esp32s3:FlashSize=16M,PSRAM=opi,FlashMode=$FLASH,PartitionScheme=custom,CPUFreq=240,USBMode=hwcdc,CDCOnBoot=default"
command -v arduino-cli >/dev/null || { echo "arduino-cli nao encontrado"; exit 1; }
arduino-cli board listall >/dev/null 2>&1 || true
if ! arduino-cli board details -b "$FQBN" >/dev/null 2>&1; then FQBN="esp32:${FQBN#espressif:}"; fi

EXTRA=()
[ -n "${ESP_X32:-}" ] && EXTRA+=(--build-property "runtime.tools.esp-x32.path=$ESP_X32")
[ -n "${ESP_LIBS:-}" ] && EXTRA+=(--build-property "runtime.tools.esp32s3-libs.path=$ESP_LIBS")
[ -n "${ESPTOOL_DIR:-}" ] && EXTRA+=(--build-property "runtime.tools.esptool_py.path=$ESPTOOL_DIR")
[ -n "${CTAGS_DIR:-}" ] && EXTRA+=(--build-property "runtime.tools.ctags.path=$CTAGS_DIR")

mkdir -p build
arduino-cli compile -b "$FQBN" --output-dir build --warnings default "${EXTRA[@]}" firmware/radar360

# Firmware de recuperacao (particao "recovery"): usa a mesma tabela de particoes e o mesmo painel
REC=build/recovery-src/recovery
mkdir -p "$REC" build/recovery
cp firmware/recovery/recovery.ino firmware/recovery/Recovery.cpp firmware/radar360/partitions.csv firmware/radar360/Board.h firmware/radar360/Display.h firmware/radar360/Panel.cpp "$REC/"
arduino-cli compile -b "$FQBN" --output-dir build/recovery --warnings default "${EXTRA[@]}" "$REC"

VER=$(grep -oP 'FW_VERSION\s+"\K[^"]+' firmware/radar360/Version.h)
cp build/radar360.ino.bin "build/radar360-v${VER}-ota${SUF}.bin"
# Imagem para gravar pelo cabo no endereco 0x0: bootloader + particoes + recuperacao + firmware + pacote visual.
# Apaga tambem a NVS (Wi-Fi/senhas: importe a copia das configuracoes depois) e o otadata.
python3 - "$VER" "$SUF" <<'PY'
import sys, struct, csv
ver, suf = sys.argv[1], sys.argv[2]
parts = {}
for row in csv.reader(l for l in open("firmware/radar360/partitions.csv") if l.strip() and not l.startswith("#")):
    row = [c.strip() for c in row]
    parts[row[0]] = (int(row[3], 16), int(row[4], 16))
boot = open("build/radar360.ino.bootloader.bin", "rb").read()
table = open("build/radar360.ino.partitions.bin", "rb").read()
rec = open("build/recovery/recovery.ino.bin", "rb").read()
app = open("build/radar360.ino.bin", "rb").read()
pack = open("pack/radar360-pack.bin", "rb").read()
pver = struct.unpack("<I", pack[8:12])[0]
for name, blob in (("recovery", rec), ("app0", app), ("assets", pack)):
    assert len(blob) <= parts[name][1], f"{name}: {len(blob)} > {parts[name][1]}"
    print(f"{name:9s} {len(blob):8d} de {parts[name][1]:8d} ({parts[name][1]-len(blob)} livres)")
end = parts["assets"][0] + len(pack)
img = bytearray(b"\xff" * ((end + 0xFFF) & ~0xFFF))
for off, blob in ((0, boot), (0x8000, table), (parts["recovery"][0], rec), (parts["app0"][0], app), (parts["assets"][0], pack)):
    img[off:off + len(blob)] = blob
open(f"build/radar360-v{ver}-cabo-0x0{suf}.bin", "wb").write(img)
open(f"build/radar360-pack-v{pver}.bin", "wb").write(pack)
print(f"radar360-v{ver}-cabo-0x0{suf}.bin: {len(img)} bytes; radar360-pack-v{pver}.bin")
PY
ls -la build/*.bin
