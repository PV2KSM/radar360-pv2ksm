#!/usr/bin/env bash
# Compila o simulador das telas (PC Linux). Uso: tools/sim/build.sh && tools/sim/sim tools/sim [filtro]
set -euo pipefail
cd "$(dirname "$0")"
LGFX=${LGFX:-$HOME/Arduino/libraries/LovyanGFX/src}
AJ=${ARDUINOJSON:-$HOME/Arduino/libraries/ArduinoJson/src}
FW0=../../firmware/radar360
mkdir -p obj src
# copia o firmware sem o Display.h de verdade (o do simulador fica em stubs/)
for f in $FW0/*.cpp $FW0/*.h $FW0/*.inc; do b=$(basename $f); [ "$b" = Display.h ] || [ "$b" = Display.cpp ] || cmp -s $f src/$b || cp $f src/; done
cp stubs/Display.h src/
FW=src
CXX="g++ -O2 -std=gnu++17 -w -DR360_SIM -DLGFX_LINUX_FB -DLGFX_USE_V1 -Istubs -I$FW -I$LGFX -I$AJ"
CC="gcc -O2 -w -I$LGFX"
for f in lgfx/v1/lgfx_v1.cpp lgfx/v1/lgfx_v1_panel.cpp lgfx/v1/LGFXBase.cpp lgfx/v1/lgfx_fonts.cpp lgfx/v1/lgfx_v1_platforms.cpp; do
  o=obj/$(basename $f .cpp).o; [ -f $o ] || $CXX -c $LGFX/$f -o $o &
done
for f in lgfx/utility/lgfx_tjpgd.c lgfx/utility/lgfx_pngle.c lgfx/utility/lgfx_qoi.c lgfx/utility/lgfx_miniz.c lgfx/utility/lgfx_qrcode.c lgfx/Fonts/efont/lgfx_efont_tw.c lgfx/Fonts/efont/lgfx_efont_kr.c lgfx/Fonts/efont/lgfx_efont_ja.c lgfx/Fonts/efont/lgfx_efont_cn.c lgfx/Fonts/IPA/lgfx_font_japan.c; do
  o=obj/$(basename $f .c).o; [ -f $o ] || $CC -c $LGFX/$f -o $o &
done
# pacote visual (no aparelho fica na particao "assets"; aqui vira um vetor no programa)
PK=../../pack/radar360-pack.bin
[ obj/PackData.o -nt $PK ] || { python3 -c "import sys;b=open(sys.argv[1],'rb').read();open('obj/PackData.cpp','w').write('#include <stdint.h>\nalignas(4) extern const uint8_t PACK_DATA[]={'+','.join(map(str,b))+'};\nextern const uint32_t PACK_SIZE='+str(len(b))+';\n')" $PK && $CXX -O0 -c obj/PackData.cpp -o obj/PackData.o; } &
[ obj/Fonts.o -nt $FW/Fonts.cpp ] || $CXX -O0 -c $FW/Fonts.cpp -o obj/Fonts.o &
[ obj/Airports.o -nt $FW/Airports.cpp ] || $CXX -O0 -c $FW/Airports.cpp -o obj/Airports.o &
wait
SRC="sim.cpp $FW/Lang.cpp $FW/Gfx.cpp $FW/Pack.cpp $FW/Sky.cpp $FW/Clock.cpp $FW/Weather.cpp $FW/Flights.cpp $FW/Photos.cpp $FW/News.cpp $FW/Dmr.cpp $FW/Feat.cpp $FW/Horo.cpp $FW/Moon.cpp $FW/RadarCard.cpp $FW/Flags.cpp $FW/Pc.cpp $FW/NetName.cpp $FW/Quotes.cpp $FW/Wifi.cpp"
$CXX -g -O0 $SRC obj/*.o -o sim -lpthread
echo "ok: tools/sim/sim"
