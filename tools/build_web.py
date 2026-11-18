#!/usr/bin/env python3
"""Compacta as paginas web (gzip) e gera firmware/radar360/WebPage.h.
- web/index.html   -> painel completo (/)
- web/control.html -> controle remoto rapido (/control), instalavel na tela de inicio
- icone (PNG 192 e 512) e manifesto do /control, desenhados aqui
"""
import gzip
import io
import json
import math
import pathlib

from PIL import Image, ImageDraw

root = pathlib.Path(__file__).resolve().parent.parent


def icon(size):
    """Icone do app: radar redondo com varredura, nas cores do painel."""
    s = size * 4
    im = Image.new("RGB", (s, s), (7, 16, 15))
    d = ImageDraw.Draw(im)
    c = s / 2
    acc = (95, 255, 210)
    # varredura (setor com rastro)
    for i in range(60):
        a0 = -150 + i * 1.0
        col = tuple(int(7 + (v - 7) * (i / 60) ** 2 * 0.55) for v in acc)
        d.pieslice([c - s * 0.36, c - s * 0.36, c + s * 0.36, c + s * 0.36], a0, a0 + 1.2, fill=col)
    for r, w in ((0.36, 0.012), (0.24, 0.008), (0.12, 0.008)):
        d.ellipse([c - s * r, c - s * r, c + s * r, c + s * r], outline=(32, 59, 55) if r < 0.3 else acc, width=max(2, int(s * w)))
    d.line([c, c, c, c - s * 0.36], fill=acc, width=int(s * 0.012))
    # dois "avioes"
    for ang, dist in ((-40, 0.27), (35, 0.18)):
        x = c + math.cos(math.radians(ang - 90)) * s * dist
        y = c + math.sin(math.radians(ang - 90)) * s * dist
        r = s * 0.022
        d.ellipse([x - r, y - r, x + r, y + r], fill=(232, 255, 249))
    im = im.resize((size, size), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return b.getvalue()


def arr(name, data):
    out = [f"static const size_t {name}_LEN = {len(data)};", f"static const uint8_t {name}[] PROGMEM = {{"]
    for i in range(0, len(data), 24):
        out.append(",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    out.append("};")
    return out


manifest = {
    "name": "Radar 360 · Controle", "short_name": "Radar 360", "start_url": "/control", "scope": "/",
    "display": "standalone", "background_color": "#07100f", "theme_color": "#07100f",
    "icons": [{"src": "/icon.png", "sizes": "192x192", "type": "image/png"},
              {"src": "/icon512.png", "sizes": "512x512", "type": "image/png", "purpose": "any maskable"}],
}

lines = ["// Gerado por tools/build_web.py a partir de web/*.html - nao edite", "#pragma once", "#include <stdint.h>"]
for name, fn in (("WEB_INDEX_GZ", "index.html"), ("WEB_CONTROL_GZ", "control.html")):
    html = (root / "web" / fn).read_bytes()
    gz = gzip.compress(html, compresslevel=9, mtime=0)
    lines += arr(name, gz)
    print(f"{fn} {len(html)} bytes -> gzip {len(gz)} bytes")
lines += arr("WEB_MANIFEST", json.dumps(manifest, ensure_ascii=False).encode())
for name, sz in (("WEB_ICON192", 192), ("WEB_ICON512", 512)):
    png = icon(sz)
    lines += arr(name, png)
    print(f"icone {sz}: {len(png)} bytes")
(root / "firmware" / "radar360" / "WebPage.h").write_text("\n".join(lines) + "\n")
