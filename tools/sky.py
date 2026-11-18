#!/usr/bin/env python3
"""Pinta os fundos realistas do Radar 360 (ceu, nuvens, morros, cidade) em 360x360.

Tudo e gerado aqui, por numpy (sem fotos de terceiros): gradiente do ceu,
brilho do sol, nuvens por ruido fractal com luz e sombra, morros em camadas
com perspectiva atmosferica e luzes da cidade a noite.
O aparelho anima por cima (chuva caindo, raios, estrelas piscando, lua).
"""
import numpy as np
from PIL import Image, ImageFilter

N = 360


def lerp(a, b, t):
    return a + (b - a) * t


def col(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], dtype=np.float32)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def octave(rng, gw, gh, w=N, h=N):
    small = rng.random((gh, gw)).astype(np.float32)
    return np.asarray(Image.fromarray(small, 'F').resize((w, h), Image.BICUBIC), dtype=np.float32)


def fbm(seed, base=(4, 3), octaves=6, persist=0.52, w=N, h=N):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    gw, gh = base
    for _ in range(octaves):
        out += amp * octave(rng, gw + 1, gh + 1, w, h)
        tot += amp
        amp *= persist
        gw, gh = gw * 2, gh * 2
    out /= tot
    out = (out - out.min()) / (out.max() - out.min() + 1e-6)
    return out


def ridge(seed, base_y, amp, freq, octs=5):
    rng = np.random.default_rng(seed)
    x = np.zeros(N, np.float32)
    a, f, tot = 1.0, freq, 0.0
    for _ in range(octs):
        pts = rng.random(f + 2).astype(np.float32)
        xs = np.linspace(0, f + 1, N)
        x += a * np.interp(xs, np.arange(f + 2), pts)
        tot += a
        a *= 0.5
        f *= 2
    x /= tot
    return base_y - amp * (x - x.min()) / (x.max() - x.min() + 1e-6)


class Scene:
    def __init__(self, top, mid, horizon, mid_at=0.55):
        yy = np.linspace(0, 1, N, dtype=np.float32)[:, None, None]
        t1 = np.clip(yy / mid_at, 0, 1)
        t2 = np.clip((yy - mid_at) / (1 - mid_at), 0, 1)
        c = np.where(yy < mid_at, lerp(col(top), col(mid), t1 ** 1.1), lerp(col(mid), col(horizon), t2 ** 0.9))
        self.img = np.broadcast_to(c, (N, N, 3)).copy()
        Y, X = np.mgrid[0:N, 0:N].astype(np.float32)
        self.X, self.Y = X, Y

    def glow(self, x, y, color, r1, a1, r2=0, a2=0):
        d = np.sqrt((self.X - x) ** 2 + (self.Y - y) ** 2)
        g = a1 * np.exp(-d / r1)
        if r2:
            g += a2 * np.exp(-(d / r2) ** 2)
        self.img += g[..., None] * col(color)
        return g

    def disc(self, x, y, r, color, soft=1.5):
        d = np.sqrt((self.X - x) ** 2 + (self.Y - y) ** 2)
        a = np.clip((r - d) / soft + 0.5, 0, 1)
        self.over(col(color), a)

    def over(self, c, a):
        a = a[..., None] if a.ndim == 2 else a
        self.img = self.img * (1 - a) + c * a

    def clouds(self, seed, cover, lit, shade, y0=0, y1=N, soft=0.12, base=(3, 4), stretch=1.0,
               light=(0, -1), opacity=1.0, rim=None, rim_from=None, octaves=6, dark_base=0.0):
        w = int(N / stretch)
        h = N
        d = fbm(seed, base, octaves, 0.55, w, h)
        if stretch != 1.0:
            d = np.asarray(Image.fromarray(d, 'F').resize((N, N), Image.BICUBIC), np.float32)
        band = smooth(y0 - 40, y0 + 30, self.Y) * (1 - smooth(y1 - 30, y1 + 40, self.Y))
        dens = smooth(cover, cover + soft, d) * band
        # luz: compara com a densidade deslocada na direcao da luz
        lx, ly = light
        sh = np.roll(np.roll(d, int(ly * 7), axis=0), int(lx * 7), axis=1)
        lit_f = np.clip(0.55 + (d - sh) * -5.5, 0, 1)            # lado voltado para a luz e mais claro
        thick = smooth(cover, cover + 0.45, d)
        lit_f = np.clip(lit_f * (1 - 0.55 * thick) + 0.25, 0, 1)
        if dark_base:
            lit_f *= 1 - dark_base * smooth(0.3, 1.0, thick)
        c = lerp(col(shade), col(lit), lit_f[..., None])
        if rim is not None:
            rx, ry, rr = rim_from
            dd = np.sqrt((self.X - rx) ** 2 + (self.Y - ry) ** 2)
            edge = dens * (1 - thick) * np.exp(-dd / rr)
            c = c + col(rim) * np.clip(edge * 2.2, 0, 1)[..., None]
        self.over(c, np.clip(dens * opacity, 0, 1))
        return dens

    def hills(self, layers, haze):
        """layers: lista de (seed, base_y, amp, freq, cor). A primeira e a mais distante."""
        n = len(layers)
        for i, (seed, by, amp, fr, c) in enumerate(layers):
            top = ridge(seed, by, amp, fr)
            m = smooth(top[None, :] - 0.8, top[None, :] + 0.8, self.Y)
            cc = lerp(col(c), col(haze), (1 - (i + 1) / n) * 0.55)
            # leve gradiente vertical (mais escuro embaixo)
            shade = 1 - 0.25 * np.clip((self.Y - top[None, :]) / 80, 0, 1)
            self.over(cc * shade[..., None], m)
        return top

    def city(self, seed, top_y, count, color='#FFC27A'):
        rng = np.random.default_rng(seed)
        layer = np.zeros((N, N), np.float32)
        for _ in range(count):
            x = rng.integers(0, N)
            y = rng.integers(int(top_y), N)
            b = rng.uniform(0.35, 1.0)
            layer[y, x] = max(layer[y, x], b)
            if rng.random() < 0.3 and x + 1 < N:
                layer[y, x + 1] = b * 0.7
        blur = np.asarray(Image.fromarray((layer * 255).astype(np.uint8), 'L').filter(ImageFilter.GaussianBlur(1.6)), np.float32) / 255
        self.img += (layer[..., None] * 0.9 + blur[..., None] * 2.2) * col(color)

    def stars(self, seed, count, ymax=260):
        rng = np.random.default_rng(seed)
        for _ in range(count):
            x, y = rng.uniform(0, N), rng.uniform(0, ymax)
            b = rng.uniform(0.15, 0.9) ** 2
            r = 0.6 + b * 0.9
            d = np.sqrt((self.X - x) ** 2 + (self.Y - y) ** 2)
            m = d < 3
            self.img[m] += (np.exp(-(d[m] / r) ** 2) * b * (1 - y / ymax * 0.6))[:, None] * np.array([0.9, 0.95, 1.0])

    def rain(self, seed, count, alpha, slant=0.18, color='#DCE6F2'):
        rng = np.random.default_rng(seed)
        layer = Image.new('L', (N, N), 0)
        from PIL import ImageDraw
        dr = ImageDraw.Draw(layer)
        for _ in range(count):
            x, y = rng.uniform(-20, N), rng.uniform(-20, N)
            L = rng.uniform(10, 26)
            dr.line([(x, y), (x - L * slant, y + L)], fill=int(rng.uniform(0.3, 1.0) * 255), width=1)
        a = np.asarray(layer.filter(ImageFilter.GaussianBlur(0.4)), np.float32) / 255 * alpha
        self.over(col(color), a)

    def haze(self, color, a_top, a_bottom, y0=0):
        t = np.clip((self.Y - y0) / (N - y0), 0, 1)
        self.over(col(color), lerp(a_top, a_bottom, t) * smooth(y0 - 40, y0 + 40, self.Y))

    def sun(self, x, y, r, dens=None, warm='#FFF4D6', a1=0.55, r1=55, wide=0.18):
        hide = 1.0 if dens is None else (1 - 0.75 * np.clip(dens, 0, 1))
        d = np.sqrt((self.X - x) ** 2 + (self.Y - y) ** 2)
        g = (a1 * np.exp(-d / r1) + 0.6 * np.exp(-(d / (r * 1.8)) ** 2)) * hide
        self.img += g[..., None] * col(warm) + (wide * np.exp(-d / 170) * hide)[..., None]
        a = np.clip((r - d) / 2.5 + 0.5, 0, 1) * hide
        self.over(col('#FFFDF6'), a)

    def readable(self, amount=0.25, cy=170, rx=150, ry=95):
        """Escurece suavemente o centro (onde ficam hora e textos)."""
        self.img = np.clip(self.img, 0, 1)
        d = ((self.X - N / 2) / rx) ** 2 + ((self.Y - cy) / ry) ** 2
        self.img *= (1 - amount * np.exp(-d * 1.4))[..., None]

    def vignette(self, amount=0.35):
        self.img = np.clip(self.img, 0, 1)
        d = np.sqrt((self.X - N / 2) ** 2 + (self.Y - N / 2) ** 2) / (N / 2)
        self.img *= (1 - amount * smooth(0.55, 1.05, d))[..., None]

    def grain(self, seed, a=0.012):
        rng = np.random.default_rng(seed)
        self.img += rng.normal(0, a, self.img.shape).astype(np.float32)

    def image(self):
        return Image.fromarray((np.clip(self.img, 0, 1) ** (1 / 1.0) * 255 + 0.5).astype(np.uint8), 'RGB')


# ---------------- cenas ----------------
GREEN_HILLS = lambda haze: [(11, 268, 46, 3, '#56708A'), (12, 292, 40, 4, '#3E6B4C'), (13, 318, 34, 5, '#2C5236'), (14, 342, 22, 7, '#1C3A25')]


def clear_day():
    s = Scene('#1A5CC8', '#3F86E2', '#BFE0F7', 0.6)
    d1 = s.clouds(3, 0.66, '#FFFFFF', '#A9BED6', y0=180, y1=272, base=(3, 8), stretch=2.4, opacity=0.95, light=(1, -1))
    d2 = s.clouds(4, 0.78, '#FFFFFF', '#C6D6E6', y0=30, y1=110, base=(3, 8), stretch=3.2, opacity=0.45, soft=0.2)
    s.sun(268, 78, 13, d1 + d2 * 0.45)
    s.hills(GREEN_HILLS('#BFE0F7'), '#BFE0F7')
    s.readable(0.34)
    s.vignette(0.3)
    return s


def partly_day():
    s = Scene('#2766C4', '#5A95DE', '#C9E2F4', 0.6)
    d = s.clouds(7, 0.5, '#FFFFFF', '#8296B0', y0=20, y1=250, base=(3, 8), stretch=1.8, opacity=1.0,
                 light=(1, -1), rim='#FFF6DE', rim_from=(250, 70, 90))
    s.sun(250, 70, 13, d)
    s.hills(GREEN_HILLS('#C9E2F4'), '#C9E2F4')
    s.readable(0.38)
    s.vignette(0.3)
    return s


def cloudy_day():
    s = Scene('#5C6E84', '#8496AA', '#C3CDD7', 0.6)
    s.clouds(21, 0.25, '#D9DFE6', '#6E7C8E', y0=0, y1=280, base=(3, 8), stretch=2.6, opacity=1.0, soft=0.35, dark_base=0.25)
    s.clouds(22, 0.55, '#E9EDF1', '#8D99A8', y0=120, y1=270, base=(3, 10), stretch=3.0, opacity=0.6, soft=0.2)
    s.hills([(11, 270, 40, 3, '#6C7C86'), (12, 294, 38, 4, '#4E6556'), (13, 320, 30, 5, '#38503F'), (14, 344, 20, 7, '#25392B')], '#C3CDD7')
    s.readable(0.32)
    s.vignette(0.32)
    return s


def rain_day():
    s = Scene('#3E4A58', '#5F6D7C', '#9AA6B1', 0.6)
    s.clouds(31, 0.22, '#A9B3BE', '#4A5563', y0=0, y1=250, base=(3, 8), stretch=2.8, soft=0.4, dark_base=0.3)
    s.haze('#8C98A4', 0.0, 0.35, 160)
    s.hills([(11, 272, 36, 3, '#5D6B72'), (12, 296, 34, 4, '#4A5A52'), (13, 322, 28, 5, '#34443A'), (14, 346, 18, 7, '#222F27')], '#8E9AA6')
    s.rain(5, 260, 0.22)
    s.readable(0.2)
    s.vignette(0.35)
    return s


def storm():
    s = Scene('#151A24', '#283140', '#4A5466', 0.6)
    s.clouds(41, 0.18, '#59657A', '#121722', y0=0, y1=260, base=(3, 8), stretch=2.2, soft=0.45, dark_base=0.45)
    s.glow(250, 120, '#B7C2FF', 70, 0.22)           # relampago dentro das nuvens
    s.clouds(42, 0.5, '#3A4558', '#0E131C', y0=150, y1=270, base=(3, 10), stretch=3.0, opacity=0.7)
    s.hills([(11, 274, 34, 3, '#2A3240'), (12, 298, 30, 4, '#1E2630'), (13, 324, 24, 5, '#161C24'), (14, 346, 18, 7, '#0E1218')], '#3A4252')
    s.rain(6, 300, 0.28, slant=0.32)
    s.vignette(0.4)
    return s


def fog():
    s = Scene('#8E9AA6', '#AEB8C1', '#D3D9DE', 0.6)
    s.clouds(51, 0.35, '#E6EAEE', '#A3AEB8', y0=0, y1=240, base=(3, 10), stretch=3.2, soft=0.5)
    s.hills([(11, 276, 30, 3, '#9AA6AE'), (12, 300, 30, 4, '#7F8F88'), (13, 324, 26, 5, '#62746A'), (14, 346, 18, 7, '#4A5B50')], '#D3D9DE')
    s.haze('#DCE1E5', 0.15, 0.75, 120)
    s.readable(0.36)
    s.vignette(0.3)
    return s


def clear_night():
    s = Scene('#03060F', '#0A1430', '#1E2E52', 0.62)
    s.stars(61, 260, 280)
    s.glow(180, 300, '#2A3F6E', 120, 0.25)
    s.clouds(62, 0.82, '#3A4A6A', '#141C30', y0=200, y1=280, base=(3, 10), stretch=3.4, opacity=0.5)
    s.hills([(11, 276, 30, 3, '#121B30'), (12, 300, 28, 4, '#0C1322'), (13, 324, 24, 5, '#080D18'), (14, 346, 18, 7, '#05080F')], '#1E2E52')
    s.city(63, 300, 380)
    s.glow(180, 380, '#FFB070', 90, 0.12)
    s.vignette(0.35)
    return s


def partly_night():
    s = Scene('#050A16', '#111C38', '#24345A', 0.62)
    s.stars(71, 180, 260)
    s.clouds(72, 0.52, '#323D58', '#121A2E', y0=20, y1=250, base=(3, 8), stretch=2.0, rim='#7184AE', rim_from=(276, 56, 80))
    s.hills([(11, 276, 30, 3, '#121B30'), (12, 300, 28, 4, '#0C1322'), (13, 324, 24, 5, '#080D18'), (14, 346, 18, 7, '#05080F')], '#24345A')
    s.city(73, 300, 360)
    s.glow(180, 380, '#FFB070', 90, 0.14)
    s.vignette(0.35)
    return s


def cloudy_night():
    s = Scene('#0C111C', '#1A2232', '#2E3A50', 0.62)
    s.clouds(81, 0.2, '#3C4760', '#101522', y0=0, y1=270, base=(3, 8), stretch=2.6, soft=0.45, dark_base=0.3)
    s.glow(180, 330, '#FF9E5A', 110, 0.16)           # luz da cidade refletida nas nuvens
    s.hills([(11, 276, 30, 3, '#151C2A'), (12, 300, 28, 4, '#0E1420'), (13, 324, 24, 5, '#0A0F18'), (14, 346, 18, 7, '#06090F')], '#2E3A50')
    s.city(83, 300, 380)
    s.vignette(0.35)
    return s


def rain_night():
    s = cloudy_night()
    s.haze('#3A4560', 0.0, 0.25, 180)
    s.rain(9, 280, 0.3, color='#B8C8E6')
    return s


def dawn():
    s = Scene('#2B3C78', '#9A6E9C', '#FFC18A', 0.55)
    s.glow(120, 268, '#FF9A6A', 170, 0.35)
    d = s.clouds(91, 0.6, '#F7B79A', '#5E4E7C', y0=40, y1=230, base=(3, 10), stretch=3.4, light=(-1, 1),
                 rim='#FFE2B8', rim_from=(120, 268, 120), opacity=0.9)
    s.sun(120, 262, 13, d, warm='#FFD9A0', a1=0.8, r1=50, wide=0.1)
    s.hills([(11, 274, 30, 3, '#5E4A6E'), (12, 298, 30, 4, '#3F3550'), (13, 322, 26, 5, '#2A2438'), (14, 346, 18, 7, '#191626')], '#F2B08C')
    s.readable(0.18)
    s.vignette(0.32)
    return s


def dusk():
    s = Scene('#1C2756', '#7A4F8E', '#FF9A62', 0.55)
    s.glow(240, 266, '#FF6E50', 180, 0.4)
    d = s.clouds(101, 0.58, '#F49C7C', '#43345F', y0=30, y1=235, base=(3, 10), stretch=3.4, light=(1, 1),
                 rim='#FFD9A0', rim_from=(240, 266, 130), opacity=0.9)
    s.sun(240, 262, 14, d, warm='#FFC07A', a1=0.9, r1=45, wide=0.1)
    s.hills([(11, 274, 30, 3, '#4E3352'), (12, 298, 30, 4, '#33243E'), (13, 322, 26, 5, '#22182C'), (14, 346, 18, 7, '#140E1C')], '#E8906E')
    s.city(103, 316, 120, '#FFD08A')
    s.readable(0.18)
    s.vignette(0.32)
    return s


SCENES = [('sky_clear_day', clear_day), ('sky_partly_day', partly_day), ('sky_cloudy_day', cloudy_day),
          ('sky_rain_day', rain_day), ('sky_storm', storm), ('sky_fog', fog), ('sky_clear_night', clear_night),
          ('sky_partly_night', partly_night), ('sky_cloudy_night', cloudy_night), ('sky_rain_night', rain_night),
          ('sky_dawn', dawn), ('sky_dusk', dusk)]


def render_all(seed_grain=1):
    out = {}
    for i, (name, fn) in enumerate(SCENES):
        s = fn()
        s.grain(seed_grain + i)
        out[name] = s.image()
    return out


if __name__ == '__main__':
    import sys
    imgs = render_all()
    sheet = Image.new('RGB', (N * 4, N * 3), 'black')
    for i, (k, im) in enumerate(imgs.items()):
        m = Image.new('L', (N, N), 0)
        from PIL import ImageDraw
        ImageDraw.Draw(m).ellipse([0, 0, N - 1, N - 1], fill=255)
        sheet.paste(im, ((i % 4) * N, (i // 4) * N), m)
    sheet.save(sys.argv[1] if len(sys.argv) > 1 else '/tmp/sky_sheet.png')
