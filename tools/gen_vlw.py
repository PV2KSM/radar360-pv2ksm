#!/usr/bin/env python3
"""Gera fontes suavizadas (anti-aliased) no formato VLW, lido pelo LovyanGFX (loadFont).

Uso: gen_vlw.py saida.h  NOME:arquivo.ttf:tamanho_px[:conjunto] ...
  conjunto: "latin" (ASCII + Latin-1, padrao) ou "digits" (0-9 : . - espaco)

Formato VLW (Processing): cabecalho 6 x int32 big-endian
  [qtd_glifos, versao=11, tamanho, 0, ascent, descent]
seguido de 7 x int32 por glifo
  [unicode, altura, largura, avanco, topo(dY), esquerda(dX), 0]
e depois os bitmaps alfa (1 byte por pixel) de todos os glifos, na mesma ordem.
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

SETS = {
    "latin": [c for c in range(0x20, 0x7F)] + [c for c in range(0xA0, 0x100)] + [0x2013, 0x2014, 0x2022, 0x2026, 0x20AC, 0x2190, 0x2191, 0x2192, 0x2193],
    "digits": [ord(c) for c in " 0123456789:.-/%°"],
    "upper": [ord(c) for c in " 0123456789:.-/%°ABCDEFGHIJKLMNOPQRSTUVWXYZ>_"],
    "latinpt": [c for c in range(0x20, 0x7F)] + [ord(c) for c in "ÀÁÂÃÄÇÈÉÊÍÓÔÕÖÚÜàáâãäçèéêíóôõöúü°·ºª«»×©"] + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2191, 0x2193],
    "signs": [ord(c) for c in " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzáãâçéêíóôõúÁÃÂÇÉÊÍÓÔÕÚ-"],
    "latin1": [c for c in range(0x20, 0x7F)] + [c for c in range(0xA0, 0x100)] + [0x2013, 0x2014, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x25B2, 0x25BC],
}


def build(ttf, size, charset, axes=None):
    font = ImageFont.truetype(ttf, size)
    if axes:
        font.set_variation_by_axes(axes)
    ascent, descent = font.getmetrics()
    glyphs = []
    for cp in sorted(set(SETS[charset])):   # o LovyanGFX procura os glifos por busca binaria
        ch = chr(cp)
        adv = round(font.getlength(ch))
        left, top, right, bottom = font.getbbox(ch, anchor="ls")  # relativo a linha de base
        w, h = max(0, right - left), max(0, bottom - top)
        bmp = b""
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-left, -top), ch, font=font, fill=255, anchor="ls")
            bmp = img.tobytes()
        # dY = distancia da linha de base ate o topo do glifo (positivo para cima)
        glyphs.append((cp, h, w, adv, -top, left, bmp))
    head = struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    meta = b"".join(struct.pack(">7i", cp, h, w, adv, dy, dx, 0) for cp, h, w, adv, dy, dx, _ in glyphs)
    bits = b"".join(g[6] for g in glyphs)
    return head + meta + bits


def c_array(name, data, comment):
    lines = [f"// {comment}", f"extern const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 32):
        lines.append(",".join(str(b) for b in data[i:i + 32]) + ",")
    lines.append("};")
    return lines


def main():
    """Gera Fonts.h (declaracoes) e Fonts.cpp (dados, uma unica copia no firmware)."""
    out, specs = sys.argv[1], sys.argv[2:]
    hdr = ["// Gerado por tools/gen_vlw.py - fontes Inter (c) The Inter Project Authors, SIL Open Font License 1.1",
           "#pragma once", "#include <stdint.h>", ""]
    src = ["// Gerado por tools/gen_vlw.py", '#include "Fonts.h"', ""]
    for spec in specs:
        parts = spec.split(":")
        name, ttf, size = parts[0], parts[1], int(parts[2])
        charset = parts[3] if len(parts) > 3 else "latin"
        data = build(ttf, size, charset)
        hdr.append(f"extern const uint8_t {name}[];   // {ttf.split('/')[-1]} {size}px ({charset}), {len(data)} bytes")
        src += c_array(name, data, f"{name}: {ttf.split('/')[-1]} {size}px") + [""]
    open(out, "w").write("\n".join(hdr) + "\n")
    open(out[:-2] + ".cpp", "w").write("\n".join(src) + "\n")


if __name__ == "__main__":
    main()
