#!/usr/bin/env python3
"""Monta o "pacote visual" do Radar 360 e grava pack/radar360-pack.bin (vai na particao "assets").

Fundos realistas (ceu por clima e hora), mostradores, fontes dos temas e
silhuetas de aeronaves, tudo num bloco so, embutido no firmware (flash).

Formato (little-endian):
  "R360PK01" | u32 versao | u32 qtd | u32 crc32(dados)
  qtd x { char nome[24] ; u32 deslocamento ; u32 tamanho }
  dados...
"""
import io
import math
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).parent))
import sky
import gen_vlw

PACK_VERSION = 2
ROOT = Path(__file__).parent
FONTS = ROOT / 'fonts'
N = 360
SS = 4   # supersampling

# ---------------------------------------------------------------- fontes
FONT_SPECS = [
    # nome, arquivo, px, conjunto, eixos
    ('vClockXL', 'InterVar.ttf', 104, 'digits', [32, 280]),
    ('vMono30', 'B612Mono-Bold.ttf', 30, 'digits', None),
    # Noticias estilo TV
    ('vOsw14', 'OswaldVar.ttf', 15, 'latinpt', [700]),
    ('vOsw18', 'OswaldVar.ttf', 19, 'latinpt', [700]),
    ('vOsw34', 'OswaldVar.ttf', 36, 'latinpt', [700]),
    ('vHead18', 'InterVar.ttf', 19, 'latinpt', [24, 800]),
    ('vInter13', 'InterVar.ttf', 13, 'latinpt', [14, 600]),
    ('vBarlow17', 'BarlowCondensed-Bold.ttf', 18, 'latinpt', None),
    ('vVT22', 'VT323-Regular.ttf', 22, 'latinpt', None),
    ('vVT48', 'VT323-Regular.ttf', 48, 'upper', None),
    ('vFell18', 'IMFell-Regular.ttf', 19, 'latinpt', None),
    ('vFellIt18', 'IMFell-Italic.ttf', 19, 'latinpt', None),
    ('vFell30', 'IMFell-Regular.ttf', 32, 'signs', None),
    ('vClockL', 'InterVar.ttf', 74, 'digits', [32, 300]),
    ('vMinimal', 'SpaceGroteskVar.ttf', 118, 'digits', [300]),
    ('vLight30', 'InterVar.ttf', 30, 'latinpt', [32, 300]),
    ('vCinzel34', 'CinzelVar.ttf', 34, 'latinpt', [600]),
    ('vCinzel20', 'CinzelVar.ttf', 20, 'latinpt', [600]),
    ('vCinzel14', 'CinzelVar.ttf', 14, 'latinpt', [700]),
    ('vSerif30', 'CormorantVar.ttf', 31, 'latinpt', [600]),
    ('vSerif22', 'CormorantVar.ttf', 23, 'latinpt', [600]),
    ('vMono14', 'B612Mono-Bold.ttf', 14, 'latinpt', None),
    ('vMono20', 'B612Mono-Bold.ttf', 20, 'latinpt', None),
]


def fonts():
    out = {}
    for name, f, px, cs, axes in FONT_SPECS:
        out[name] = gen_vlw.build(str(FONTS / f), px, cs, axes)
    return out


# ---------------------------------------------------------------- silhuetas
# Vista de cima, nariz para cima, num quadrado de 100 x 100.
CATS = ['wide', 'narrow', 'regional', 'turboprop', 'bizjet', 'light', 'heli']


def _wing(d, cx, y, half_root, span, sweep, cr, ct, fill):
    for s in (1, -1):
        d.polygon([(cx + s * half_root, y), (cx + s * span / 2, y + sweep), (cx + s * span / 2, y + sweep + ct),
                   (cx + s * half_root, y + cr)], fill=fill)


def _fus(d, cx, top, w, L, fill, tail_taper=0.55):
    # fuselagem: nariz redondo, cauda afinando
    d.ellipse([cx - w / 2, top, cx + w / 2, top + w * 1.6], fill=fill)
    d.polygon([(cx - w / 2, top + w * 0.8), (cx + w / 2, top + w * 0.8), (cx + w * tail_taper / 2, top + L),
               (cx - w * tail_taper / 2, top + L)], fill=fill)


def _eng(d, x, y, w, h, fill):
    d.rounded_rectangle([x - w / 2, y, x + w / 2, y + h], radius=w / 2.2, fill=fill)


def silhouette(kind, size):
    S = 100 * SS
    im = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(im)
    k = SS
    F = 255
    c = 50 * k
    if kind == 'wide':
        _wing(d, c, 40 * k, 4 * k, 98 * k, 24 * k, 21 * k, 5 * k, F)
        _eng(d, c - 22 * k, 46 * k, 7 * k, 13 * k, F); _eng(d, c + 22 * k, 46 * k, 7 * k, 13 * k, F)
        _wing(d, c, 82 * k, 3 * k, 38 * k, 9 * k, 10 * k, 4 * k, F)
        _fus(d, c, 2 * k, 12 * k, 94 * k, F)
    elif kind == 'narrow':
        _wing(d, c, 42 * k, 3.5 * k, 86 * k, 19 * k, 17 * k, 4 * k, F)
        _eng(d, c - 17 * k, 46 * k, 6 * k, 11 * k, F); _eng(d, c + 17 * k, 46 * k, 6 * k, 11 * k, F)
        _wing(d, c, 82 * k, 3 * k, 32 * k, 8 * k, 9 * k, 3.5 * k, F)
        _fus(d, c, 6 * k, 10 * k, 88 * k, F)
    elif kind == 'regional':
        _wing(d, c, 44 * k, 3 * k, 76 * k, 15 * k, 15 * k, 4 * k, F)
        _eng(d, c - 15 * k, 47 * k, 5.5 * k, 10 * k, F); _eng(d, c + 15 * k, 47 * k, 5.5 * k, 10 * k, F)
        _wing(d, c, 83 * k, 2.5 * k, 28 * k, 7 * k, 8 * k, 3 * k, F)
        _fus(d, c, 10 * k, 9 * k, 82 * k, F)
    elif kind == 'turboprop':
        _wing(d, c, 36 * k, 3 * k, 92 * k, 1 * k, 11 * k, 7 * k, F)
        for s in (-1, 1):
            _eng(d, c + s * 18 * k, 28 * k, 6 * k, 20 * k, F)
            d.ellipse([c + s * 18 * k - 10 * k, 26 * k, c + s * 18 * k + 10 * k, 29 * k], fill=170)   # helice
        _wing(d, c, 84 * k, 2.5 * k, 32 * k, 2 * k, 8 * k, 6 * k, F)
        _fus(d, c, 10 * k, 9 * k, 84 * k, F)
    elif kind == 'bizjet':
        _wing(d, c, 46 * k, 3 * k, 66 * k, 15 * k, 14 * k, 3.5 * k, F)
        _eng(d, c - 9 * k, 64 * k, 6 * k, 13 * k, F); _eng(d, c + 9 * k, 64 * k, 6 * k, 13 * k, F)
        _wing(d, c, 84 * k, 2 * k, 28 * k, 8 * k, 8 * k, 3 * k, F)
        _fus(d, c, 12 * k, 8 * k, 80 * k, F)
    elif kind == 'light':
        _wing(d, c, 34 * k, 3 * k, 90 * k, 0, 12 * k, 12 * k, F)
        _wing(d, c, 82 * k, 2 * k, 34 * k, 0, 9 * k, 7 * k, F)
        _fus(d, c, 14 * k, 9 * k, 78 * k, F, 0.35)
        d.ellipse([c - 11 * k, 12 * k, c + 11 * k, 15 * k], fill=170)
    elif kind == 'heli':
        d.ellipse([c - 46 * k, 4 * k, c + 46 * k, 96 * k], outline=90, width=int(2.2 * k))
        d.ellipse([c - 46 * k, 4 * k, c + 46 * k, 96 * k], fill=28)
        d.ellipse([c - 12 * k, 22 * k, c + 12 * k, 62 * k], fill=F)
        d.rounded_rectangle([c - 3 * k, 55 * k, c + 3 * k, 92 * k], radius=2 * k, fill=F)
        d.rounded_rectangle([c - 12 * k, 86 * k, c + 12 * k, 91 * k], radius=2 * k, fill=F)
        for a in (20, 110):
            r = math.radians(a)
            dx, dy = math.cos(r) * 46 * k, math.sin(r) * 46 * k
            d.line([(c - dx, 50 * k - dy), (c + dx, 50 * k + dy)], fill=F, width=int(3.4 * k))
        d.ellipse([c - 4 * k, 46 * k, c + 4 * k, 54 * k], fill=F)
    return im


def silhouettes(small=30, big=150, steps=36):
    blob_small = bytearray()
    blob_big = bytearray()
    previews = []
    for kind in CATS:
        hi = silhouette(kind, 100)
        for i in range(steps):
            r = hi.rotate(-i * 360 / steps, resample=Image.BICUBIC)
            blob_small += r.resize((small, small), Image.LANCZOS).tobytes()
        b = hi.resize((big, big), Image.LANCZOS)
        blob_big += b.tobytes()
        previews.append(b)
    return bytes(blob_small), bytes(blob_big), previews


# ---------------------------------------------------------------- fundos de tema
def studio_bg():
    """Fundo de telejornal: estudio azul com luzes desfocadas e um telao com mapa."""
    k = SS; Sz = N * k
    s = sky.Scene('#173470', '#0B1838', '#050A18', 0.5)
    rng = np.random.default_rng(4)
    for _ in range(16):
        c = ['#5090FF', '#FFFFFF', '#3CC8FF', '#FF5A5A'][rng.integers(0, 4)]
        s.glow(rng.uniform(0, N), rng.uniform(20, 220), c, rng.uniform(10, 26), 0.22, rng.uniform(8, 18), 0.25)
    s.vignette(0.35)
    im = s.image().resize((Sz, Sz), Image.BICUBIC)
    d = ImageDraw.Draw(im, 'RGBA')
    d.rounded_rectangle([36 * k, 64 * k, 324 * k, 196 * k], radius=6 * k, fill=(30, 70, 150, 120), outline=(120, 170, 255, 110), width=k)
    # mapa estilizado no telao
    pts = [(90, 168), (104, 136), (138, 128), (162, 104), (200, 98), (236, 110), (262, 132), (258, 162), (224, 180), (176, 184), (128, 180)]
    d.polygon([(x * k, y * k) for x, y in pts], fill=(46, 111, 208, 120), outline=(143, 192, 255, 140))
    for i in range(6):
        d.line([(60 * k, (76 + i * 20) * k), (300 * k, (76 + i * 20) * k)], fill=(120, 170, 255, 25), width=k)
    d.ellipse([(186) * k, (134) * k, (198) * k, (146) * k], fill=(255, 213, 79, 255))
    d.ellipse([(178) * k, (126) * k, (206) * k, (154) * k], outline=(255, 213, 79, 150), width=k)
    return im.resize((N, N), Image.LANCZOS)


def urgent_bg():
    s = sky.Scene('#2A0000', '#1A0000', '#100000', 0.5)
    for y in range(0, N, 20):
        s.img[y:y + 10] += np.array([0.06, 0.0, 0.0], np.float32)
    s.glow(180, 118, '#FF1E1E', 120, 0.25)
    s.vignette(0.5)
    return s.image()


def channel24_bg():
    im = Image.new('RGB', (N * SS, N * SS)); d = ImageDraw.Draw(im)
    for y in range(N * SS):
        t = y / (N * SS); d.line([(0, y), (N * SS, y)], fill=(int(14 - 6 * t), int(26 - 10 * t), int(46 - 16 * t)))
    for x in range(0, 520, 60):
        d.line([(x * SS, 0), ((x - 120) * SS, N * SS)], fill=(20, 38, 64), width=18 * SS)
    return im.resize((N, N), Image.LANCZOS)


def aurora_bg():
    s = sky.Scene('#03040E', '#0A0C2A', '#141238', 0.6)
    s.stars(5, 220, 360)
    Y, X = s.Y, s.X
    # cortinas de aurora: faixas verticais moduladas por ruido
    n1 = sky.fbm(201, (12, 2), 5, 0.5)
    for cx, cy, w, h, c1, c2, a in [(110, 90, 210, 120, '#7C4DFF', '#22D3EE', 0.55), (250, 130, 200, 110, '#22D3EE', '#34D399', 0.38)]:
        dd = ((X - cx) / w) ** 2 + ((Y - cy) / h) ** 2
        env = np.exp(-dd * 2.2)
        streak = 0.45 + 0.55 * n1
        t = np.clip((Y - (cy - h)) / (2 * h), 0, 1)[..., None]
        color = sky.lerp(sky.col(c1), sky.col(c2), t)
        s.img += (env * streak * a)[..., None] * color
    s.glow(180, 420, '#F2C66D', 140, 0.10)
    s.vignette(0.45)
    s.grain(7, 0.01)
    return s.image()


def obsidian_bg():
    s = sky.Scene('#0B1A18', '#071311', '#030807', 0.5)
    s.glow(180, 120, '#0F2E29', 140, 0.6)
    s.vignette(0.5)
    s.grain(8, 0.008)
    return s.image()


def _tick_ring(d, cx, cy, r1, r2, n, every, w1, w2, c1, c2, k):
    for i in range(n):
        a = math.radians(i * 360 / n - 90)
        big = i % every == 0
        ra = (r1 - (6 * k if big else 0))
        d.line([(cx + math.cos(a) * ra, cy + math.sin(a) * ra), (cx + math.cos(a) * r2, cy + math.sin(a) * r2)],
               fill=c1 if big else c2, width=int((w1 if big else w2) * k))


def classic_dial():
    k = SS
    S = N * k
    base = aurora_bg().resize((S, S), Image.BICUBIC)
    base = Image.blend(base, Image.new('RGB', (S, S), (6, 6, 22)), 0.45)
    d = ImageDraw.Draw(base)
    c = S / 2
    gold, gold2 = (242, 198, 109), (150, 122, 70)
    d.ellipse([c - 170 * k, c - 170 * k, c + 170 * k, c + 170 * k], outline=gold, width=int(1.6 * k))
    d.ellipse([c - 163 * k, c - 163 * k, c + 163 * k, c + 163 * k], outline=gold2, width=int(0.8 * k))
    _tick_ring(d, c, c, 158 * k, 150 * k, 60, 5, 3.2, 1.1, gold, gold2, k)
    f = ImageFont.truetype(str(FONTS / 'CinzelVar.ttf'), 24 * k)
    f.set_variation_by_axes([600])
    for lab, ang in (('XII', 0), ('III', 90), ('IX', 270)):
        a = math.radians(ang - 90)
        d.text((c + math.cos(a) * 128 * k, c + math.sin(a) * 128 * k), lab, font=f, fill=gold, anchor='mm')
    # mostrador pequeno do clima (baixo)
    d.ellipse([c - 38 * k, c + 52 * k, c + 38 * k, c + 128 * k], fill=(10, 10, 32), outline=gold2, width=int(1.2 * k))
    return base.resize((N, N), Image.LANCZOS)


def cockpit_dial():
    k = SS
    S = N * k
    s = sky.Scene('#121516', '#0C0E0F', '#08090A', 0.5)
    s.glow(150, 110, '#2A3036', 160, 0.25)
    s.vignette(0.5)
    base = s.image().resize((S, S), Image.BICUBIC)
    d = ImageDraw.Draw(base)
    c = S / 2
    d.ellipse([c - 172 * k, c - 172 * k, c + 172 * k, c + 172 * k], outline=(58, 64, 69), width=int(3 * k))
    _tick_ring(d, c, c, 162 * k, 150 * k, 60, 5, 3.4, 1.3, (242, 242, 238), (106, 112, 117), k)
    f = ImageFont.truetype(str(FONTS / 'B612Mono-Bold.ttf'), 19 * k)
    for i in range(1, 13):
        a = math.radians(i * 30 - 90)
        d.text((c + math.cos(a) * 130 * k, c + math.sin(a) * 130 * k), str(i), font=f, fill=(242, 242, 238), anchor='mm')
    return base.resize((N, N), Image.LANCZOS)


def nib(raw):
    """Alfa de 8 bits -> 4 bits (2 pixels por byte, o primeiro no nibble alto)."""
    a = np.frombuffer(raw, np.uint8).astype(np.uint16)
    a = ((a * 15 + 127) // 255).astype(np.uint8)
    if len(a) % 2:
        a = np.append(a, 0)
    return bytes((a[0::2] << 4) | a[1::2])


def jpeg(im, q=88):
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=q, subsampling=0 if q >= 90 else 2, optimize=True, progressive=False)
    return b.getvalue()


# ---------------------------------------------------------------- v0.4: horoscopo, vintage, aviao, animacoes
ZOD = [chr(c) for c in range(0x2648, 0x2654)]


def zodiac_masks(size):
    out = bytearray()
    f = ImageFont.truetype(str(FONTS / 'NotoSansSymbols.ttf'), int(size * 0.86 * SS))
    for ch in ZOD:
        im = Image.new('L', (size * SS, size * SS), 0)
        ImageDraw.Draw(im).text((size * SS / 2, size * SS / 2), ch, font=f, fill=255, anchor='mm')
        out += im.resize((size, size), Image.LANCZOS).tobytes()
    return bytes(out)


def wisps():
    """Faixa de nuvens finas (alfa) 512x120 que o aparelho desliza devagar."""
    w, h = 512, 120
    rng = np.random.default_rng(77)
    n = sky.fbm(78, (6, 2), 6, 0.55, w, h)
    # repete nas bordas (rolagem sem emenda)
    n = (n + np.roll(n[:, ::-1], 0, axis=1)) / 2
    Y = np.linspace(-1, 1, h)[:, None]
    band = np.exp(-(Y / 0.62) ** 2)
    a = sky.smooth(0.45, 0.85, n) * band
    return (np.clip(a, 0, 1) * 255).astype(np.uint8).tobytes()


def parchment_bg():
    k = SS; S = N * k
    s = sky.Scene('#F3E6C8', '#EBD9B3', '#C9AE7C', 0.55)
    s.glow(180, 170, '#FBF3DE', 160, 0.35)
    n = sky.fbm(91, (5, 5), 6, 0.6)
    s.img *= (0.93 + 0.07 * n)[..., None]
    for (x, y, r) in ((70, 110, 50), (280, 260, 60), (240, 70, 30)):
        d = np.sqrt((s.X - x) ** 2 + (s.Y - y) ** 2)
        s.img *= (1 - 0.10 * np.exp(-(d / r) ** 2))[..., None]
    s.vignette(0.35)
    im = s.image().resize((S, S), Image.BICUBIC)
    d = ImageDraw.Draw(im)
    c = S / 2; ink = (90, 58, 26)
    for r, w in ((168, 2.2), (162, 0.9)):
        d.ellipse([c - r * k, c - r * k, c + r * k, c + r * k], outline=ink, width=int(w * k))
    for i in range(120):
        a = math.radians(i * 3)
        x, y = c + math.cos(a) * 154 * k, c + math.sin(a) * 154 * k
        d.ellipse([x - 0.8 * k, y - 0.8 * k, x + 0.8 * k, y + 0.8 * k], fill=ink)
    for a in (0, 90, 180, 270):
        x, y = c + math.sin(math.radians(a)) * 162 * k, c - math.cos(math.radians(a)) * 162 * k
        d.polygon([(x, y - 6 * k), (x + 6 * k, y), (x, y + 6 * k), (x - 6 * k, y)], fill=ink)
    for side in (-1, 1):
        for yy, dy, x0, ln in ((150, -1, 74, 62), (270, 1, 104, 40)):
            pts = [(c + side * (x0 + t * ln) * k, (yy + dy * 7 * math.sin(t * math.pi * 1.4)) * k) for t in np.linspace(0, 1, 40)]
            d.line(pts, fill=ink, width=int(1.1 * k))
            ex, ey = pts[-1]
            d.ellipse([ex - 2.5 * k, ey - 2.5 * k, ex + 2.5 * k, ey + 2.5 * k], fill=ink)
    return im.resize((N, N), Image.LANCZOS)


def constellation_bg():
    s = sky.Scene('#0F1A33', '#070B18', '#02040A', 0.5)
    s.stars(41, 260, 360)
    s.glow(180, 130, '#16284A', 150, 0.35)
    s.vignette(0.4)
    s.grain(5, 0.008)
    return s.image()


def _bezel(base, k, screws=True):
    d = ImageDraw.Draw(base)
    c = base.size[0] / 2
    if screws:
        for a in (45, 135, 225, 315):
            x, y = c + math.sin(math.radians(a)) * 160 * k, c - math.cos(math.radians(a)) * 160 * k
            d.ellipse([x - 7 * k, y - 7 * k, x + 7 * k, y + 7 * k], fill=(42, 45, 48), outline=(85, 89, 94), width=int(k))
            r = math.radians(a * 1.7)
            d.line([(x - math.cos(r) * 5 * k, y - math.sin(r) * 5 * k), (x + math.cos(r) * 5 * k, y + math.sin(r) * 5 * k)], fill=(17, 17, 17), width=int(1.8 * k))
    return d


def panel_base():
    s = sky.Scene('#1E2124', '#16181A', '#0E0F10', 0.5)
    n = sky.fbm(55, (40, 40), 3, 0.5)
    s.img *= (0.96 + 0.06 * n)[..., None]      # pintura texturizada do painel
    s.vignette(0.45)
    return s.image()


def av_clock_dial():
    k = SS; S = N * k
    im = panel_base().resize((S, S), Image.BICUBIC)
    d = _bezel(im, k); c = S / 2
    d.ellipse([c - 142 * k, c - 142 * k, c + 142 * k, c + 142 * k], fill=(8, 9, 10), outline=(58, 62, 66), width=int(4 * k))
    cream = (232, 228, 208)
    for i in range(60):
        a = math.radians(i * 6 - 90); big = i % 5 == 0
        r0 = (116 if big else 126) * k
        d.line([(c + math.cos(a) * r0, c + math.sin(a) * r0), (c + math.cos(a) * 134 * k, c + math.sin(a) * 134 * k)], fill=cream, width=int((2.6 if big else 0.9) * k))
    f = ImageFont.truetype(str(FONTS / 'B612-Bold.ttf'), 20 * k)
    for i in range(1, 13):
        a = math.radians(i * 30 - 90)
        d.text((c + math.cos(a) * 99 * k, c + math.sin(a) * 99 * k), str(i), font=f, fill=cream, anchor='mm')
    f2 = ImageFont.truetype(str(FONTS / 'B612-Bold.ttf'), 10 * k)
    d.text((c, c - 42 * k), 'C L O C K', font=f2, fill=(138, 143, 148), anchor='mm')
    d.rounded_rectangle([c - 34 * k, c + 34 * k, c + 34 * k, c + 58 * k], radius=3 * k, fill=(0, 0, 0), outline=(85, 89, 94), width=int(k))
    return im.resize((N, N), Image.LANCZOS)


def altimeter_dial():
    k = SS; S = N * k
    im = panel_base().resize((S, S), Image.BICUBIC)
    d = _bezel(im, k); c = S / 2
    d.ellipse([c - 142 * k, c - 142 * k, c + 142 * k, c + 142 * k], fill=(8, 9, 10), outline=(58, 62, 66), width=int(4 * k))
    cream = (232, 228, 208)
    for i in range(50):
        a = math.radians(i * 7.2 - 90); big = i % 5 == 0
        r0 = (114 if big else 126) * k
        d.line([(c + math.cos(a) * r0, c + math.sin(a) * r0), (c + math.cos(a) * 134 * k, c + math.sin(a) * 134 * k)], fill=cream, width=int((2.6 if big else 0.9) * k))
    f = ImageFont.truetype(str(FONTS / 'B612-Bold.ttf'), 24 * k)
    for i in range(10):
        a = math.radians(i * 36 - 90)
        d.text((c + math.cos(a) * 97 * k, c + math.sin(a) * 97 * k), str(i), font=f, fill=cream, anchor='mm')
    # faixa listrada de alerta
    d.rectangle([c - 64 * k, c - 36 * k, c - 52 * k, c + 30 * k], fill=cream)
    for j in range(13):
        y0 = c - 42 * k + j * 6 * k
        d.polygon([(c - 64 * k, y0 + 6 * k), (c - 52 * k, y0), (c - 52 * k, y0 + 3 * k), (c - 64 * k, y0 + 9 * k)], fill=(17, 17, 17))
    d.rectangle([c - 70 * k, c - 42 * k, c - 64 * k, c + 36 * k], fill=(8, 9, 10)); d.rectangle([c - 52 * k, c - 42 * k, c - 46 * k, c + 36 * k], fill=(8, 9, 10))
    d.rectangle([c - 64 * k, c - 42 * k, c - 52 * k, c - 36 * k], fill=(8, 9, 10)); d.rectangle([c - 64 * k, c + 30 * k, c - 52 * k, c + 36 * k], fill=(8, 9, 10))
    d.rectangle([c - 64 * k, c - 36 * k, c - 52 * k, c + 30 * k], outline=(85, 89, 94), width=int(k))
    d.rounded_rectangle([c + 34 * k, c - 12 * k, c + 88 * k, c + 12 * k], radius=2 * k, fill=(0, 0, 0), outline=(85, 89, 94), width=int(k))
    f2 = ImageFont.truetype(str(FONTS / 'B612-Bold.ttf'), 9 * k)
    d.text((c, c + 52 * k), 'A L T  ·  H O R A', font=f2, fill=(138, 143, 148), anchor='mm')
    d.text((c, c + 66 * k), 'X 1 0 0 0  F T', font=f2, fill=(90, 95, 100), anchor='mm')
    return im.resize((N, N), Image.LANCZOS)


def av_bezel():
    k = SS; S = N * k
    im = panel_base().resize((S, S), Image.BICUBIC)
    _bezel(im, k)
    return im.resize((N, N), Image.LANCZOS)


def hsi_rose(size=280):
    k = SS; S = size * k
    im = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(im); c = S / 2
    for i in range(72):
        a = math.radians(i * 5 - 90); big = i % 2 == 0
        r0 = ((size / 2 - 16) if big else (size / 2 - 10)) * k
        d.line([(c + math.cos(a) * r0, c + math.sin(a) * r0), (c + math.cos(a) * (size / 2 - 3) * k, c + math.sin(a) * (size / 2 - 3) * k)], fill=255, width=int((1.8 if big else 0.9) * k))
    labs = {0: 'N', 3: '3', 6: '6', 9: 'E', 12: '12', 15: '15', 18: 'S', 21: '21', 24: '24', 27: 'W', 30: '30', 33: '33'}
    for kk, l in labs.items():
        big = l in 'NESW'
        f = ImageFont.truetype(str(FONTS / 'B612Mono-Bold.ttf'), (20 if big else 15) * k)
        g = Image.new('L', (40 * k, 40 * k), 0)
        ImageDraw.Draw(g).text((20 * k, 20 * k), l, font=f, fill=255, anchor='mm')
        g = g.rotate(-kk * 10, resample=Image.BICUBIC)
        a = math.radians(kk * 10 - 90)
        x, y = c + math.cos(a) * (size / 2 - 36) * k, c + math.sin(a) * (size / 2 - 36) * k
        im.paste(255, (int(x - 20 * k), int(y - 20 * k)), g)
    return im.resize((size, size), Image.LANCZOS).tobytes()


def deco_dial():
    k = SS; S = N * k
    s = sky.Scene('#1A1A1A', '#0F0F0F', '#050505', 0.5)
    s.vignette(0.5)
    im = s.image().resize((S, S), Image.BICUBIC)
    d = ImageDraw.Draw(im); c = S / 2
    gold, gold2, dim = (232, 200, 120), (201, 165, 90), (70, 58, 34)
    for i in range(48):
        a = math.radians(i * 7.5 - 90)
        d.line([(c + math.cos(a) * 34 * k, c + math.sin(a) * 34 * k), (c + math.cos(a) * 150 * k, c + math.sin(a) * 150 * k)],
               fill=dim if i % 2 else (96, 78, 44), width=int((1.2 if i % 2 else 2.2) * k))
    for r, w in ((172, 3), (164, 1), (118, 0.8)):
        d.ellipse([c - r * k, c - r * k, c + r * k, c + r * k], outline=gold2, width=int(w * k))
    for i in range(12):
        a = math.radians(i * 30 - 90)
        x, y = c + math.cos(a) * 149 * k, c + math.sin(a) * 149 * k
        if i % 3 == 0:
            continue
        pts = [(0, -7), (4, 0), (0, 7), (-4, 0)]
        ca, sa = math.cos(math.radians(i * 30)), math.sin(math.radians(i * 30))
        d.polygon([(x + (px * ca - py * sa) * k, y + (px * sa + py * ca) * k) for px, py in pts], fill=gold)
    f = ImageFont.truetype(str(FONTS / 'PoiretOne-Regular.ttf'), 34 * k)
    for lab, i in (('12', 0), ('3', 3), ('6', 6), ('9', 9)):
        a = math.radians(i * 30 - 90)
        x, y = c + math.cos(a) * 140 * k, c + math.sin(a) * 140 * k
        d.ellipse([x - 22 * k, y - 22 * k, x + 22 * k, y + 22 * k], fill=(12, 12, 12), outline=gold2, width=int(1 * k))
        d.text((x, y + 1 * k), lab, font=f, fill=gold, anchor='mm')
    return im.resize((N, N), Image.LANCZOS)


def pocket_dial():
    k = SS; S = N * k
    s = sky.Scene('#FBF5E6', '#F1E6CC', '#D9C49A', 0.5)
    s.glow(150, 130, '#FFFDF4', 120, 0.25)
    n = sky.fbm(33, (6, 6), 6, 0.6)
    s.img *= (0.95 + 0.05 * n)[..., None]
    for (x, y, r) in ((250, 110, 26), (95, 260, 30), (300, 230, 16)):
        d_ = np.sqrt((s.X - x) ** 2 + (s.Y - y) ** 2)
        s.img *= (1 - 0.09 * np.exp(-(d_ / r) ** 2))[..., None]   # manchas do tempo
    im = s.image().resize((S, S), Image.BICUBIC)
    d = ImageDraw.Draw(im); c = S / 2
    ink = (42, 34, 24)
    d.ellipse([c - 176 * k, c - 176 * k, c + 176 * k, c + 176 * k], outline=(140, 106, 46), width=int(7 * k))
    d.ellipse([c - 171 * k, c - 171 * k, c + 171 * k, c + 171 * k], outline=(233, 211, 154), width=int(1.5 * k))
    for r in (150, 160):
        d.ellipse([c - r * k, c - r * k, c + r * k, c + r * k], outline=ink, width=int(0.9 * k))
    for i in range(60):
        a = math.radians(i * 6 - 90)
        d.line([(c + math.cos(a) * 150 * k, c + math.sin(a) * 150 * k), (c + math.cos(a) * 160 * k, c + math.sin(a) * 160 * k)], fill=ink, width=int((1.8 if i % 5 == 0 else 0.7) * k))
    f = ImageFont.truetype(str(FONTS / 'IMFell-Regular.ttf'), 24 * k)
    rom = ['XII', 'I', 'II', 'III', 'IIII', 'V', 'VI', 'VII', 'VIII', 'IX', 'X', 'XI']
    for i, rn in enumerate(rom):
        if i == 6:
            continue
        a = math.radians(i * 30 - 90)
        g = Image.new('L', (90 * k, 50 * k), 0)
        ImageDraw.Draw(g).text((45 * k, 25 * k), rn, font=f, fill=255, anchor='mm')
        g = g.rotate(-i * 30 if i not in (4, 5, 7, 8) else -i * 30 + 180, resample=Image.BICUBIC, expand=False)
        x, y = c + math.cos(a) * 129 * k, c + math.sin(a) * 129 * k
        im.paste(ink, (int(x - 45 * k), int(y - 25 * k)), g)
    cx2, cy2 = c, c + 68 * k
    d.ellipse([cx2 - 30 * k, cy2 - 30 * k, cx2 + 30 * k, cy2 + 30 * k], outline=ink, width=int(0.9 * k))
    for i in range(12):
        a = math.radians(i * 30 - 90)
        d.line([(cx2 + math.cos(a) * 25 * k, cy2 + math.sin(a) * 25 * k), (cx2 + math.cos(a) * 29 * k, cy2 + math.sin(a) * 29 * k)], fill=ink, width=int(0.8 * k))
    f2 = ImageFont.truetype(str(FONTS / 'IMFell-Regular.ttf'), 15 * k)
    d.text((c, c - 66 * k), 'Radar 360', font=f2, fill=ink, anchor='mm')
    return im.resize((N, N), Image.LANCZOS)


def radio_dial():
    k = SS; S = N * k
    s = sky.Scene('#5A3A22', '#3B2414', '#20120A', 0.5)
    n = sky.fbm(61, (3, 30), 5, 0.6)            # veios da baquelite
    s.img *= (0.88 + 0.18 * n)[..., None]
    s.glow(180, 70, '#7A5232', 140, 0.25)
    s.vignette(0.4)
    im = s.image().resize((S, S), Image.BICUBIC)
    d = ImageDraw.Draw(im); c = S / 2
    brass = (201, 160, 106)
    d.ellipse([c - 172 * k, c - 172 * k, c + 172 * k, c + 172 * k], outline=brass, width=int(2 * k))
    # janela ambar com brilho de valvula
    win = Image.new('L', (S, S), 0)
    wd = ImageDraw.Draw(win)
    wd.pieslice([c - 142 * k, c - 142 * k, c + 142 * k, c + 142 * k], -160, -20, fill=255)
    wd.ellipse([c - 82 * k, c - 82 * k, c + 82 * k, c + 82 * k], fill=0)
    wd.rectangle([0, c, S, S], fill=0)
    amb = sky.Scene('#FFE6A8', '#F5B95A', '#B9772A', 0.6)
    amb.glow(180, 120, '#FFF2CC', 80, 0.5)
    amb_im = amb.image().resize((S, S), Image.BICUBIC)
    im.paste(amb_im, (0, 0), win)
    d.arc([c - 142 * k, c - 142 * k, c + 142 * k, c + 142 * k], -160, -20, fill=brass, width=int(2 * k))
    d.arc([c - 82 * k, c - 82 * k, c + 82 * k, c + 82 * k], -160, -20, fill=brass, width=int(2 * k))
    for a0 in (-160, -20):
        a = math.radians(a0)
        d.line([(c + math.cos(a) * 82 * k, c + math.sin(a) * 82 * k), (c + math.cos(a) * 142 * k, c + math.sin(a) * 142 * k)], fill=brass, width=int(2 * k))
    ink = (59, 36, 20)
    f = ImageFont.truetype(str(FONTS / 'CinzelVar.ttf'), 13 * k); f.set_variation_by_axes([700])
    for i in range(13):
        a = math.radians(-150 + i * 10)
        r0 = (126 if i % 2 else 116) * k
        d.line([(c + math.cos(a) * 134 * k, c + math.sin(a) * 134 * k), (c + math.cos(a) * r0, c + math.sin(a) * r0)], fill=ink, width=int((1.8 if i % 2 == 0 else 0.9) * k))
        if i % 2 == 0:
            d.text((c + math.cos(a) * 102 * k, c + math.sin(a) * 102 * k), f'{i * 5:02d}', font=f, fill=ink, anchor='mm')
    # grade do alto-falante
    for y in range(212, 322, 9):
        hw = math.sqrt(max(0, 150 ** 2 - (y - 180) ** 2)) * 0.8
        d.line([(c - hw * k, y * k), (c + hw * k, y * k)], fill=(26, 14, 6), width=int(4.5 * k))
        d.line([(c - hw * k, (y + 2) * k), (c + hw * k, (y + 2) * k)], fill=(110, 74, 46), width=int(0.8 * k))
    for x in (112, 248):
        d.ellipse([x * k - 17 * k, 190 * k - 17 * k, x * k + 17 * k, 190 * k + 17 * k], fill=(42, 26, 14), outline=brass, width=int(1.5 * k))
    return im.resize((N, N), Image.LANCZOS)


def build_v4(entries):
    entries.append(('zod_40', nib(zodiac_masks(40))))
    entries.append(('zod_96', nib(zodiac_masks(96))))
    entries.append(('wisps', nib(wisps())))
    entries.append(('bg_parchment', jpeg(parchment_bg(), 90)))
    entries.append(('bg_constel', jpeg(constellation_bg(), 90)))
    entries.append(('dial_pocket', jpeg(pocket_dial(), 92)))
    entries.append(('dial_alt', jpeg(altimeter_dial(), 92)))
    entries.append(('bezel_av', jpeg(av_bezel(), 90)))
    entries.append(('rose_280', nib(hsi_rose(280))))


# ---------------------------------------------------------------- pacote
def build(out_path, preview_path=None):
    entries = []
    import sky2
    for name, im in sky2.render_all().items():
        entries.append((name, jpeg(im, 88)))
    entries.append(('bg_aurora', jpeg(aurora_bg(), 90)))
    entries.append(('bg_studio', jpeg(studio_bg(), 90)))
    entries.append(('bg_urgent', jpeg(urgent_bg(), 88)))
    entries.append(('bg_24h', jpeg(channel24_bg(), 90)))
    entries.append(('bg_obsidian', jpeg(obsidian_bg(), 90)))
    entries.append(('dial_classic', jpeg(classic_dial(), 92)))
    entries.append(('dial_cockpit', jpeg(cockpit_dial(), 92)))
    small, big, prev = silhouettes()
    entries.append(('sil_30', nib(small)))
    entries.append(('sil_150', nib(big)))
    from moon_tex import moon_tex
    entries.append(('moon_tex', moon_tex(132).tobytes()))
    from flags_pack import build as flags_build
    fl, co, pf = flags_build()
    entries.append(('flags', fl))
    entries.append(('countries', co))
    entries.append(('callprefix', pf))
    build_v4(entries)
    for k, v in fonts().items():
        entries.append((k, v))

    head = 8 + 12 + len(entries) * 32
    data = b''
    table = b''
    for name, blob in entries:
        assert len(name) < 24
        # alinha em 4 bytes
        while (head + len(data)) % 4:
            data += b'\0'
        table += struct.pack('<24sII', name.encode(), head + len(data), len(blob))
        data += blob
    crc = zlib.crc32(data) & 0xFFFFFFFF
    pack = b'R360PK01' + struct.pack('<III', PACK_VERSION, len(entries), crc) + table + data
    if out_path.endswith('.cpp'):
        lines = ['// Gerado por tools/build_pack.py - nao editar', '#include <stdint.h>', '',
                 f'alignas(4) extern const uint8_t PACK_DATA[] = {{']
        for i in range(0, len(pack), 40):
            lines.append(','.join(str(b) for b in pack[i:i + 40]) + ',')
        lines += ['};', f'extern const uint32_t PACK_SIZE = {len(pack)};', '']
        Path(out_path).write_text('\n'.join(lines))
    else:
        Path(out_path).write_bytes(pack)
    print(f'{out_path}: {len(pack)} bytes, {len(entries)} itens')
    for name, blob in entries:
        print(f'  {name:16s} {len(blob):8d}')

    if preview_path:
        sheet = Image.new('RGB', (N * 4, N * 2 + 160), (20, 20, 20))
        for i, (n, im) in enumerate([('a', aurora_bg()), ('o', obsidian_bg()), ('c', classic_dial()), ('k', cockpit_dial())]):
            sheet.paste(im, (i * N, 0))
        for i, p in enumerate(prev):
            sheet.paste(Image.merge('RGB', (p, p, p)), (i * 160 + 10, N + 10))
        Image.open(io.BytesIO(small)) if False else None
        sheet.save(preview_path)


if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else str(ROOT.parent / 'pack/radar360-pack.bin'), sys.argv[2] if len(sys.argv) > 2 else None)
