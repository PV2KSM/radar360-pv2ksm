"""Textura da Lua (albedo, face visivel) para a tela da Lua.

Mares nas posicoes reais (latitude/longitude selenograficas aproximadas), crateras e raios de
Tycho e Copernico. Projecao ortografica com o norte para cima e o leste (Crisium) a direita,
como se ve do hemisferio norte. Saida: 8 bits de cinza, size x size.
"""
import numpy as np

MARIA = [  # lat, lon, raio (graus), escuridao
    (34, -17, 17, 1.0),   # Imbrium
    (20, -52, 15, 0.9), (4, -58, 13, 0.85), (34, -46, 11, 0.85), (-5, -44, 10, 0.8), (12, -40, 10, 0.7), (40, -60, 8, 0.7),   # Procellarum
    (28, 17, 9, 1.0),     # Serenitatis
    (8.5, 31, 12, 1.0), (18, 26, 6, 0.7),   # Tranquillitatis
    (17, 59, 7, 1.05),    # Crisium
    (-6, 51, 9, 0.9), (0, 44, 6, 0.7),     # Fecunditatis
    (-15, 35, 5, 0.9),    # Nectaris
    (-21, -17, 9, 0.85),  # Nubium
    (-24, -39, 5, 0.95),  # Humorum
    (13, 4, 4, 0.8),      # Vaporum
    (-10, -23, 5, 0.75),  # Cognitum
    (7, -31, 7, 0.7),     # Insularum
    (2, 1, 3, 0.6),       # Sinus Medii
] + [(56, l, 5, 0.8) for l in range(-44, 46, 6)]   # Frigoris (faixa ao norte)

BRIGHT = [  # crateras claras: lat, lon, raio, brilho, raios
    (-43, -11, 1.6, 0.35, 1.0),   # Tycho
    (10, -20, 1.8, 0.25, 0.55),   # Copernicus
    (8, -38, 1.0, 0.25, 0.35),    # Kepler
    (24, -47, 0.9, 0.40, 0.2),    # Aristarchus
    (-9, -1, 1.2, 0.12, 0.0),     # Ptolemaeus (escuro-ish, so borda)
]


def _noise(rng, size, cells):
    g = rng.random((cells + 1, cells + 1))
    xs = np.linspace(0, cells, size, endpoint=False)
    i = xs.astype(int)
    f = xs - i
    f = f * f * (3 - 2 * f)
    a = g[i][:, i]; b = g[i][:, i + 1]; c = g[i + 1][:, i]; d = g[i + 1][:, i + 1]
    fx = f[None, :]; fy = f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def moon_tex(size=160):
    rng = np.random.default_rng(1969)
    r = size / 2 - 0.5
    yy, xx = np.mgrid[0:size, 0:size]
    X = (xx - size / 2 + 0.5) / r
    Y = -(yy - size / 2 + 0.5) / r
    R2 = X * X + Y * Y
    inside = R2 <= 1
    Z = np.sqrt(np.clip(1 - R2, 0, 1))
    lat = np.degrees(np.arcsin(np.clip(Y, -1, 1)))
    lon = np.degrees(np.arctan2(X, Z))

    def angdist(la, lo):
        la1, lo1 = np.radians(lat), np.radians(lon)
        la2, lo2 = np.radians(la), np.radians(lo)
        c = np.sin(la1) * np.sin(la2) + np.cos(la1) * np.cos(la2) * np.cos(lo1 - lo2)
        return np.degrees(np.arccos(np.clip(c, -1, 1)))

    n = 0.55 * _noise(rng, size, 6) + 0.3 * _noise(rng, size, 14) + 0.15 * _noise(rng, size, 40)
    alb = 0.80 + (n - 0.5) * 0.16
    # campo dos mares: soma de manchas suaves com coordenadas "entortadas" por ruido, depois corte suave
    wl = (_noise(rng, size, 5) - 0.5) * 7 + (_noise(rng, size, 13) - 0.5) * 4
    wo = (_noise(rng, size, 5) - 0.5) * 7 + (_noise(rng, size, 13) - 0.5) * 4
    lat0, lon0 = lat, lon
    lat = lat0 + wl; lon = lon0 + wo
    field = np.zeros_like(alb)
    for la, lo, rad, k in MARIA:
        d = angdist(la, lo)
        field += k * np.exp(-(d / (rad * 1.05)) ** 2)
    lat, lon = lat0, lon0
    mare = np.clip((field - 0.38) / 0.5, 0, 1)
    mare = mare * mare * (3 - 2 * mare)
    mn = _noise(rng, size, 18)
    alb = alb * (1 - (0.28 + 0.16 * mn) * mare)
    # crateras pequenas (aneis claros com fundo levemente escuro), mais nas terras altas
    for _ in range(420):
        la = np.degrees(np.arcsin(rng.uniform(-0.97, 0.97)))
        lo = rng.uniform(-88, 88)
        rad = rng.uniform(0.5, 2.6) ** 1.25
        d = angdist(la, lo)
        ring = np.exp(-((d - rad) / (rad * 0.28 + 0.2)) ** 2)
        bowl = np.clip(1 - d / rad, 0, 1)
        amp = (0.018 if rng.random() < 0.75 else 0.04) * (1 - 0.6 * float(mare[min(size-1,int((0.5-np.sin(np.radians(la))/2)*size)), min(size-1,int((0.5+np.cos(np.radians(la))*np.sin(np.radians(lo))/2)*size))]))
        alb += ring * amp - bowl * amp * 0.5
    # crateras claras e raios
    for la, lo, rad, br, rays in BRIGHT:
        d = angdist(la, lo)
        alb += br * np.exp(-(d / rad) ** 2)
        if rays:
            ang = np.arctan2(lat - la, (lon - lo) * np.cos(np.radians(la)))
            streak = (0.5 + 0.5 * np.cos(ang * 11 + 1.3)) * (0.5 + 0.5 * np.cos(ang * 7 + 0.4))
            alb += rays * 0.07 * streak * np.exp(-d / 22) * (d > rad)
    alb = np.clip(alb, 0, 1.15) / 1.15
    out = np.where(inside, alb, 0)
    return (np.clip(out, 0, 1) * 255).astype(np.uint8)


if __name__ == '__main__':
    from PIL import Image
    Image.fromarray(moon_tex(320)).save('/tmp/moon_tex.png')
