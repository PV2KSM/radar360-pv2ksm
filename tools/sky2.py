#!/usr/bin/env python3
"""Ceus mais realistas para o Radar 360 (versao 2).

Tudo pintado por codigo (sem fotos de terceiros), mas com tecnicas de fotografia/render:
- ceu com espalhamento (azul mais escuro no alto, horizonte claro e enevoado)
- nuvens em perspectiva (deck que some no horizonte), ruido fractal com distorcao de dominio,
  luz calculada por profundidade optica em direcao ao sol (volume), borda prateada contra o sol
- morros com textura de mata, linha de arvores, perspectiva atmosferica e nevoa nos vales
- noite com Via Lactea, estrelas de cores variadas e luzes da cidade com brilho no ceu
Renderiza em 540 px e reduz para 360 (bordas suaves).
"""
import numpy as np
from PIL import Image, ImageFilter

S = 540          # resolucao interna
OUT = 360


def col(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], np.float32)


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


# ---------------- ruido continuo (vetorizado) ----------------
def _hash(xi, yi, seed):
    h = (xi.astype(np.uint32) * np.uint32(374761393) + yi.astype(np.uint32) * np.uint32(668265263) + np.uint32(seed * 1442695041 & 0xFFFFFFFF))
    h = (h ^ (h >> np.uint32(13))) * np.uint32(1274126177)
    h = h ^ (h >> np.uint32(16))
    return (h & np.uint32(0xFFFFFF)).astype(np.float32) / 16777216.0


def vnoise(x, y, seed):
    xi = np.floor(x); yi = np.floor(y)
    xf = x - xi; yf = y - yi
    xi = xi.astype(np.int64); yi = yi.astype(np.int64)
    u = xf * xf * (3 - 2 * xf); v = yf * yf * (3 - 2 * yf)
    a = _hash(xi, yi, seed); b = _hash(xi + 1, yi, seed); c = _hash(xi, yi + 1, seed); d = _hash(xi + 1, yi + 1, seed)
    return lerp(lerp(a, b, u), lerp(c, d, u), v)


def fbm(x, y, seed, octaves=6, gain=0.5, lac=2.03):
    s = np.zeros_like(x); a = 1.0; tot = 0.0; f = 1.0
    for o in range(octaves):
        s += a * vnoise(x * f, y * f, seed + o * 17)
        tot += a; a *= gain; f *= lac
    return s / tot


def warped(x, y, seed, octaves=6, warp=0.9):
    qx = fbm(x + 1.7, y + 9.2, seed + 101, 4)
    qy = fbm(x + 8.3, y + 2.8, seed + 202, 4)
    return fbm(x + warp * qx, y + warp * qy, seed, octaves)


class Sky:
    def __init__(self, zenith, horizon, hz=0.74, haze='#FFFFFF', haze_a=0.0):
        self.hz = hz * S
        Y, X = np.mgrid[0:S, 0:S].astype(np.float32)
        self.X, self.Y = X, Y
        t = np.clip((self.hz - Y) / self.hz, 0, 1)[..., None]
        self.img = lerp(col(horizon), col(zenith), t ** 0.55)
        if haze_a:
            band = np.exp(-np.clip(self.hz - Y, 0, None) / (0.07 * S))[..., None]
            self.img = lerp(self.img, col(haze), band * haze_a)
        self.sun_xy = None

    # ---- utilidades ----
    def over(self, c, a):
        a = a[..., None] if a.ndim == 2 else a
        self.img = self.img * (1 - a) + c * a

    def add(self, c, a):
        a = a[..., None] if a.ndim == 2 else a
        self.img = self.img + c * a

    def glow(self, x, y, color, r, a, gauss=False):
        d = np.sqrt((self.X - x * S / OUT) ** 2 + (self.Y - y * S / OUT) ** 2) / (S / OUT)
        g = a * (np.exp(-(d / r) ** 2) if gauss else np.exp(-d / r))
        self.add(col(color), g)

    # ---- sol ----
    def sun(self, x, y, r=12, color='#FFF3D6', strength=1.0, hide=None):
        self.sun_xy = (x, y)
        d = np.sqrt((self.X - x * S / OUT) ** 2 + (self.Y - y * S / OUT) ** 2) / (S / OUT)
        h = 1.0 if hide is None else (1 - 0.85 * hide)
        g = strength * (0.9 * np.exp(-(d / (r * 1.6)) ** 2) + 0.55 * np.exp(-d / 28) + 0.25 * np.exp(-d / 110))
        self.add(col(color), g * h)
        disc = np.clip((r - d) * 1.2 + 0.5, 0, 1) * h
        self.over(np.array([3.0, 2.95, 2.8], np.float32), disc)

    # ---- nuvens em perspectiva ----
    def clouds(self, seed, cover, soft=0.18, scale=1.0, base_y=0.0, top_y=None, light='#FFFFFF', shade='#8E9BB0',
               ambient=None, absorb=3.2, opacity=1.0, stretch=2.6, drift=(0.0, 0.0), silver=0.9, sun_dir=None,
               dark_bottom=0.0, puffy=0.55, warp=0.9, horizon_fade=0.06, planar=True):
        """Deck de nuvens visto de baixo; mais longe = mais achatado e menor, perto do horizonte."""
        hz = self.hz
        top = 0 if top_y is None else top_y * S
        # perspectiva continua: mais achatada e menor perto do horizonte, redonda no alto
        if planar:
            dy = np.clip(hz - self.Y, 0.0, None) / S
            depth = 1.0 / (dy + 0.28)
            u = (self.X - S / 2) / S * depth * 3.2 * scale + drift[0]
            v = depth * 3.6 * scale * (2.6 / stretch) + drift[1]
        else:
            # ceu de por do sol: deck em perspectiva forte (raios convergindo para o horizonte)
            dy = np.clip(hz - self.Y, 1.0, None) / S + 0.035
            depth = 1.0 / dy
            u = (self.X - S / 2) / S * depth * 0.55 * scale + drift[0]
            v = (depth * 0.42 + (hz - self.Y) / S * 2.2) * scale / stretch + drift[1]
        n = warped(u * 1.3, v * 2.0, seed, 6, warp)
        # cumulus: topo fofo, base mais definida
        # bordas mais fofas (cumulus): realca os blocos grandes
        big = fbm(u * 0.7, v * 1.0, seed + 7, 3)
        n = n * (1 - puffy * 0.5) + big * puffy * 0.5
        dens = smooth(cover, cover + soft, n)
        mask = smooth(-0.5, 6, hz - self.Y) * smooth(top - 20, top + 30, self.Y) if top else smooth(-0.5, 6, hz - self.Y)
        dens *= mask
        # luz: profundidade optica em direcao ao sol (em coordenadas da nuvem)
        if sun_dir is None:
            sx, sy = self.sun_xy if self.sun_xy else (OUT * 0.7, OUT * 0.2)
            sun_dir = ((sx - OUT / 2) / OUT * 0.6, -0.25 if sy < OUT * 0.6 else 0.25)
        od = np.zeros_like(n)
        for k in (1, 2, 3, 4):
            uu = u + sun_dir[0] * 0.09 * k; vv = v + sun_dir[1] * 0.09 * k
            nn = warped(uu * 1.3, vv * 2.0, seed, 4, warp)
            od += smooth(cover, cover + soft, nn)
        lightf = 0.25 + 0.75 * np.exp(-od * absorb / 4)
        thick = smooth(cover, cover + soft * 2.5, n)
        powder = 1 - np.exp(-thick * 4)
        L = col(light); D = col(shade); A = col(ambient) if ambient else D
        c = lerp(A * (0.85 + 0.15 * (1 - thick))[..., None], L, (lightf * (0.35 + 0.65 * powder))[..., None])
        if dark_bottom:
            c = c * (1 - dark_bottom * thick[..., None])
        # borda prateada contra o sol
        if self.sun_xy and silver:
            sx, sy = self.sun_xy
            ds = np.sqrt((self.X - sx * S / OUT) ** 2 + (self.Y - sy * S / OUT) ** 2) / S
            rim = np.exp(-ds / 0.12) * (1 - thick) * dens
            c = c + col('#FFF6E0') * (silver * rim)[..., None]
        # longe = mais claro e azulado (ar no meio)
        far = np.exp(-np.clip(hz - self.Y, 0, None) / (horizon_fade * S))
        hazec = self.img
        c = lerp(c, hazec, (far * 0.75)[..., None])
        a = np.clip(dens * opacity, 0, 1)
        self.over(c, a)
        return a

    # ---- cortinas de chuva saindo da base das nuvens ----
    def rain_shafts(self, seed, count=5, color='#7D8796', alpha=0.35):
        rng = np.random.default_rng(seed)
        for _ in range(count):
            x0 = rng.uniform(0, S); w = rng.uniform(25, 90) * S / OUT; sl = rng.uniform(-0.25, 0.1)
            xc = x0 + (self.Y - self.hz * 0.35) * sl
            m = np.exp(-((self.X - xc) / w) ** 2) * smooth(self.hz * 0.3, self.hz * 0.6, self.Y)
            streak = 0.6 + 0.4 * vnoise((self.X - self.Y * sl) * 0.15, self.Y * 0.004, seed + _)
            self.over(col(color), m * streak * alpha)

    # ---- relevo ----
    def ridge(self, seed, base, amp, freq, rough=0.25):
        x = np.linspace(0, freq, S, dtype=np.float32)
        r = np.zeros(S, np.float32); a = 1.0; f = 1.0; tot = 0
        for o in range(6):
            r += a * vnoise(x * f, np.full(S, 3.3 + o, np.float32), seed + o)
            tot += a; a *= 0.5 + rough * 0.1; f *= 2.1
        r /= tot
        r = (r - r.min()) / (r.max() - r.min() + 1e-6)
        return base * S / OUT - amp * S / OUT * r

    def land(self, layers, haze, trees=True, mist=0.0, mist_color='#FFFFFF', lights=None):
        """layers: (seed, base_y, amp, freq, cor) do mais longe para o mais perto."""
        n = len(layers)
        H = col(haze)
        tex = fbm(self.X * 0.06, self.Y * 0.09, 999, 5)
        for i, (seed, by, amp, fr, c) in enumerate(layers):
            top = self.ridge(seed, by, amp, fr)
            if trees and i >= n - 2:
                bumps = np.abs(np.sin(np.linspace(0, fr * 40, S) + vnoise(np.linspace(0, 30, S), np.zeros(S) + i, seed) * 6))
                top = top - (2.2 + 2.5 * bumps) * (i - n + 3)
            m = smooth(top[None, :] - 0.7, top[None, :] + 0.7, self.Y)
            depth = (i + 1) / n
            base_c = col(c) * (0.9 + 0.22 * (tex[..., None] - 0.5) * depth)
            shade = 1 - 0.35 * np.clip((self.Y - top[None, :]) / (60 * S / OUT), 0, 1) * depth
            cc = lerp(base_c * shade[..., None], H, (1 - depth) ** 1.3 * 0.75)
            self.over(cc, m)
            if mist and i < n - 1:
                # nevoa assentada no vale atras da proxima camada
                band = np.exp(-np.clip(self.Y - top[None, :], 0, None) / (22 * S / OUT)) * m
                self.over(col(mist_color), band * mist * (1 - depth * 0.5))
            if lights is not None and i >= n - 2:
                self._lights(lights, top, m, seed)

    def _lights(self, color, top, m, seed):
        rng = np.random.default_rng(seed)
        layer = np.zeros((S, S), np.float32)
        cl = vnoise(self.X * 0.03, self.Y * 0.05, seed)                 # bairros (aglomerados)
        prob = smooth(0.45, 0.8, cl) * m * smooth(0, 10, self.Y - top[None, :] - 4)
        pts = rng.random((S, S)) < prob * 0.07
        layer[pts] = rng.uniform(0.4, 1.0, pts.sum())
        sm = np.asarray(Image.fromarray((layer * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.2)), np.float32) / 255
        warm = col(color); white = col('#FFF2D8')
        self.add(warm, sm * 2.2 + layer * 0.5)
        self.add(white, layer * 0.5)

    # ---- noite ----
    def stars(self, seed, count, ymax=0.8):
        rng = np.random.default_rng(seed)
        layer = np.zeros((S, S, 3), np.float32)
        for _ in range(count):
            x, y = rng.uniform(0, S), rng.uniform(0, ymax * self.hz)
            b = rng.uniform(0.1, 1.0) ** 3
            t = rng.choice([col('#CFE0FF'), col('#FFFFFF'), col('#FFE6C2'), col('#FFD0A8')])
            xi, yi = int(x), int(y)
            layer[yi, xi] += t * b * 1.6
        bl = np.stack([np.asarray(Image.fromarray(np.clip(layer[..., k] * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8)), np.float32) / 255 for k in range(3)], -1)
        fade = smooth(0, 0.35 * self.hz, self.hz - self.Y)[..., None]
        self.img += (layer * 0.6 + bl * 1.6) * fade

    def milky_way(self, seed, a=0.22):
        # faixa diagonal com nuvens de poeira escura
        d = ((self.X - S * 0.2) * 0.62 - (self.Y - S * 0.05)) / S
        band = np.exp(-(d / 0.11) ** 2)
        n = fbm(self.X * 0.012, self.Y * 0.012, seed, 6)
        dust = smooth(0.45, 0.75, fbm(self.X * 0.02 + 3, self.Y * 0.02, seed + 9, 5))
        lum = band * (0.4 + 0.8 * n) * (1 - 0.7 * dust) * smooth(0, 0.3 * self.hz, self.hz - self.Y)
        self.add(col('#C8C2E8'), lum * a)
        self.stars(seed + 50, 900, 0.9)

    def city_glow(self, color='#FF9E5A', a=0.35):
        self.add(col(color), np.exp(-np.clip(self.hz - self.Y, 0, None) / (0.12 * S)) * a * (self.Y < self.hz + 30))

    def rain(self, seed, count, alpha, slant=0.18, color='#DCE6F2'):
        from PIL import ImageDraw
        rng = np.random.default_rng(seed)
        layer = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(layer)
        for _ in range(count):
            x, y = rng.uniform(-30, S), rng.uniform(-30, S)
            L = rng.uniform(14, 40)
            dr.line([(x, y), (x - L * slant, y + L)], fill=int(rng.uniform(0.25, 1.0) * 255), width=1)
        a = np.asarray(layer.filter(ImageFilter.GaussianBlur(0.6)), np.float32) / 255 * alpha
        self.over(col(color), a)

    def haze(self, color, a_top, a_bottom, y0=0.0):
        t = np.clip((self.Y - y0 * S) / (S - y0 * S), 0, 1)
        self.over(col(color), lerp(a_top, a_bottom, t) * smooth(y0 * S - 40, y0 * S + 40, self.Y))

    def readable(self, amount=0.25, cy=168, rx=155, ry=95):
        self.img = np.clip(self.img, 0, None)
        d = ((self.X / (S / OUT) - OUT / 2) / rx) ** 2 + ((self.Y / (S / OUT) - cy) / ry) ** 2
        self.img *= (1 - amount * np.exp(-d * 1.4))[..., None]

    def grade(self, contrast=1.06, sat=1.08, lift=0.0, gamma=1.0):
        m = self.img.mean(-1, keepdims=True)
        self.img = lerp(m, self.img, sat)
        self.img = (self.img - 0.5) * contrast + 0.5 + lift
        self.img = np.clip(self.img, 0, None) ** gamma

    def vignette(self, amount=0.3):
        d = np.sqrt((self.X - S / 2) ** 2 + (self.Y - S / 2) ** 2) / (S / 2)
        self.img *= (1 - amount * smooth(0.6, 1.05, d))[..., None]

    def image(self, seed=0):
        # tonemap suave nos claros (sol estourado vira branco sem degraus)
        x = np.clip(self.img, 0, None)
        # tonemap pela luminancia (sem anel colorido em volta do sol)
        lum = x.max(-1, keepdims=True)
        k = np.where(lum > 0.8, (0.8 + (1 - np.exp(-(lum - 0.8) * 3)) * 0.2) / np.maximum(lum, 1e-6), 1.0)
        x = x * k
        x = lerp(x, np.ones_like(x), np.clip((lum - 1.0) * 0.6, 0, 1))   # estourado vira branco
        rng = np.random.default_rng(seed)
        x = x + rng.normal(0, 0.008, x.shape).astype(np.float32)
        im = Image.fromarray((np.clip(x, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGB')
        return im.resize((OUT, OUT), Image.LANCZOS)


GREEN = [(11, 262, 40, 3, '#6F8CB0'), (12, 284, 38, 4, '#4E7A62'), (13, 308, 32, 5, '#2F5A36'), (14, 334, 26, 7, '#1A3A1F')]
NIGHT = [(11, 266, 34, 3, '#141D33'), (12, 288, 30, 4, '#0E1526'), (13, 312, 26, 5, '#0A0F1B'), (14, 336, 22, 7, '#060910')]


def clear_day():
    s = Sky('#1556C2', '#BFDDF5', haze='#F2F7FB', haze_a=0.6)
    s.sun_xy = (262, 72)
    s.clouds(3, 0.6, 0.16, scale=1.1, light='#FFFFFF', shade='#A9BCD4', opacity=0.95, stretch=3.0, puffy=0.8, absorb=2.0, silver=0.4)
    s.sun(262, 72, 11)
    s.land(GREEN, '#BCD6EC', mist=0.15)
    s.readable(0.3); s.grade(1.05, 1.06); s.vignette(0.28)
    return s


def partly_day():
    s = Sky('#1B5FC6', '#C6E0F4', haze='#F4F8FB', haze_a=0.55)
    s.sun_xy = (250, 66)
    s.clouds(7, 0.46, 0.16, scale=0.9, light='#FFFFFF', shade='#8FA2BC', ambient='#A9B9CE', opacity=1.0, stretch=2.2, puffy=0.8, absorb=2.0, silver=0.5)
    s.sun(250, 66, 11, strength=0.8)
    s.land(GREEN, '#C2DAEE', mist=0.12)
    s.readable(0.36); s.grade(1.06, 1.05); s.vignette(0.28)
    return s


def cloudy_day():
    s = Sky('#6F7F93', '#C9D1DA', haze='#DDE3E9', haze_a=0.5)
    s.clouds(21, 0.3, 0.42, scale=0.9, light='#F2F4F7', shade='#5E6B7C', ambient='#6E7B8C', opacity=1.0, stretch=2.0,
             puffy=0.65, sun_dir=(0.1, -0.3), absorb=2.6, silver=0, dark_bottom=0.15)
    s.clouds(22, 0.6, 0.25, scale=1.6, light='#F0F3F6', shade='#8E9AAA', opacity=0.55, stretch=3.0, sun_dir=(0.1, -0.3), silver=0)
    s.land([(11, 266, 38, 3, '#77848D'), (12, 288, 36, 4, '#56695B'), (13, 310, 30, 5, '#3E5443'), (14, 336, 24, 7, '#2A3C2E')], '#C9D1DA', mist=0.2)
    s.readable(0.12); s.grade(1.08, 0.95); s.vignette(0.3)
    return s


def rain_day():
    s = Sky('#3C4756', '#8E9AA6', haze='#9AA5B0', haze_a=0.6)
    s.clouds(31, 0.25, 0.45, scale=0.85, light='#B4BEC9', shade='#323C49', opacity=1.0, stretch=1.8, puffy=0.6,
             sun_dir=(0.0, -0.3), absorb=2.4, silver=0, dark_bottom=0.3)
    s.rain_shafts(32, 6, '#6E7887', 0.35)
    s.land([(11, 268, 34, 3, '#66727A'), (12, 290, 32, 4, '#4C5C52'), (13, 312, 28, 5, '#36463C'), (14, 336, 22, 7, '#232F27')], '#8E9AA6', mist=0.3, mist_color='#A8B2BC')
    s.rain(5, 380, 0.2)
    s.readable(0.18); s.grade(1.05, 0.92); s.vignette(0.35)
    return s


def storm():
    s = Sky('#121722', '#3E475A', haze='#4C566A', haze_a=0.5)
    s.clouds(41, 0.22, 0.45, scale=0.8, light='#77839C', shade='#0C1018', opacity=1.0, stretch=1.6, puffy=0.75,
             sun_dir=(0.0, -0.4), absorb=3.0, silver=0, dark_bottom=0.5)
    s.glow(250, 120, '#AFC0FF', 60, 0.25)
    s.rain_shafts(42, 7, '#2A3240', 0.5)
    s.land([(11, 270, 30, 3, '#262E3B'), (12, 292, 28, 4, '#1B222C'), (13, 314, 24, 5, '#141A22'), (14, 338, 20, 7, '#0C1016')], '#3A4252', trees=True)
    s.rain(6, 460, 0.26, slant=0.32)
    s.grade(1.08, 0.9); s.vignette(0.42)
    return s


def fog():
    s = Sky('#8C98A4', '#D6DCE1', haze='#E4E8EB', haze_a=0.8)
    s.clouds(51, 0.3, 0.5, scale=0.9, light='#EEF1F3', shade='#A9B3BD', opacity=0.8, stretch=3.0, silver=0, sun_dir=(0, -0.3))
    s.land([(11, 270, 30, 3, '#A5AFB6'), (12, 294, 28, 4, '#8C9993'), (13, 318, 26, 5, '#6E7E73'), (14, 340, 20, 7, '#56655A')], '#D6DCE1', mist=0.55, mist_color='#E8ECEF')
    s.haze('#DDE2E6', 0.1, 0.6, 0.35)
    s.readable(0.1); s.grade(1.04, 0.85); s.vignette(0.28)
    return s


def clear_night():
    s = Sky('#02040C', '#16244A', haze='#2A3A64', haze_a=0.5)
    s.milky_way(61, 0.22)
    s.stars(62, 500)
    s.city_glow('#C87A4A', 0.25)
    s.land(NIGHT, '#1C2A4C', lights='#FFB060')
    s.grade(1.05, 1.05); s.vignette(0.35)
    return s


def partly_night():
    s = Sky('#040814', '#1A2850', haze='#2C3C66', haze_a=0.5)
    s.stars(71, 600)
    s.sun_xy = (276, 56)
    s.clouds(72, 0.5, 0.22, scale=0.9, light='#5A6A90', shade='#121A2E', opacity=0.95, stretch=2.2, puffy=0.7,
             silver=0.7, absorb=2.6)
    s.sun_xy = None
    s.city_glow('#C87A4A', 0.22)
    s.land(NIGHT, '#1E2C50', lights='#FFB060')
    s.grade(1.05, 1.0); s.vignette(0.35)
    return s


def cloudy_night():
    s = Sky('#0B1019', '#2E3850', haze='#3A4258', haze_a=0.5)
    s.clouds(81, 0.26, 0.45, scale=0.85, light='#4E5A78', shade='#0C111C', opacity=1.0, stretch=2.0, puffy=0.65, silver=0,
             sun_dir=(0, -0.3), absorb=2.6, dark_bottom=0.2)
    s.city_glow('#FF8E4A', 0.4)
    s.land(NIGHT, '#2A3048', lights='#FFB060')
    s.grade(1.05, 1.0); s.vignette(0.36)
    return s


def rain_night():
    s = cloudy_night()
    s.rain_shafts(83, 5, '#1E2434', 0.4)
    s.rain(9, 420, 0.28, color='#B8C8E6')
    return s


def dawn():
    s = Sky('#22346E', '#FFB37E', haze='#FFC99A', haze_a=0.7)
    s.glow(118, 266, '#FF9060', 120, 0.45)
    s.sun(118, 262, 11, color='#FFD9A0', strength=1.2)
    s.clouds(91, 0.5, 0.2, planar=False, scale=1.0, light='#FFC7A0', shade='#4E4470', ambient='#6A5A8A', opacity=0.95, stretch=3.2,
             puffy=0.6, sun_dir=(-0.3, 0.35), silver=1.4)
    s.land([(11, 266, 30, 3, '#6E5A7E'), (12, 288, 30, 4, '#463A5A'), (13, 312, 26, 5, '#2C2640'), (14, 336, 22, 7, '#191626')], '#F2B08C', mist=0.35, mist_color='#F6C7A8')
    s.readable(0.2); s.grade(1.06, 1.1); s.vignette(0.32)
    return s


def dusk():
    s = Sky('#171F4E', '#FF8A55', haze='#FFB070', haze_a=0.75)
    s.glow(240, 266, '#FF6040', 150, 0.5)
    s.sun(240, 262, 12, color='#FFC07A', strength=1.25)
    s.clouds(101, 0.47, 0.2, planar=False, scale=1.0, light='#FFA27A', shade='#3A2D58', ambient='#5A4278', opacity=0.95, stretch=3.2,
             puffy=0.6, sun_dir=(0.3, 0.35), silver=1.5)
    s.land([(11, 266, 30, 3, '#5A3A5E'), (12, 288, 30, 4, '#3A2848'), (13, 312, 26, 5, '#241A30'), (14, 336, 22, 7, '#140E1C')], '#E8906E', lights='#FFD08A')
    s.readable(0.2); s.grade(1.06, 1.1); s.vignette(0.32)
    return s


SCENES = [('sky_clear_day', clear_day), ('sky_partly_day', partly_day), ('sky_cloudy_day', cloudy_day),
          ('sky_rain_day', rain_day), ('sky_storm', storm), ('sky_fog', fog), ('sky_clear_night', clear_night),
          ('sky_partly_night', partly_night), ('sky_cloudy_night', cloudy_night), ('sky_rain_night', rain_night),
          ('sky_dawn', dawn), ('sky_dusk', dusk)]


def render_all(seed_grain=1, only=None):
    out = {}
    for i, (name, fn) in enumerate(SCENES):
        if only and only not in name:
            continue
        out[name] = fn().image(seed_grain + i)
    return out


if __name__ == '__main__':
    import sys, time
    from PIL import ImageDraw
    only = sys.argv[2] if len(sys.argv) > 2 else None
    t = time.time()
    imgs = render_all(only=only)
    print(f'{len(imgs)} cenas em {time.time() - t:.1f}s')
    cols = min(4, len(imgs)); rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new('RGB', (OUT * cols, OUT * rows), 'black')
    m = Image.new('L', (OUT, OUT), 0); ImageDraw.Draw(m).ellipse([0, 0, OUT - 1, OUT - 1], fill=255)
    for i, im in enumerate(imgs.values()):
        sheet.paste(im, ((i % cols) * OUT, (i // cols) * OUT), m)
    sheet.save(sys.argv[1] if len(sys.argv) > 1 else '/tmp/sky2.png')
