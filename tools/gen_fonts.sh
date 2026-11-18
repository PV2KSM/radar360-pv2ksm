#!/usr/bin/env bash
# Regera firmware/radar360/Fonts.h (fontes Inter suavizadas, SIL OFL 1.1)
cd "$(dirname "$0")/.."
python3 tools/gen_vlw.py firmware/radar360/Fonts.h \
  FontClock:tools/fonts/InterDisplay-Regular.ttf:110:digits \
  FontTitle:tools/fonts/Inter-SemiBold.ttf:30 \
  FontBody:tools/fonts/Inter-Medium.ttf:22 \
  FontSmall:tools/fonts/Inter-Medium.ttf:17
