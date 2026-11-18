"""Bandeiras dos paises para a tela ID DMR.

Fonte: flag-icons (licenca MIT, tools/flags/LICENSE-flag-icons), SVG 4:3.
Saida para o pacote:
  'flags'     : "FLG1" u16 n | n x (char code[2], u32 off, u16 len) | PNGs 120x90 com paleta
  'countries' : linhas "br|Brazil|Brasil\n" (nome em ingles do RadioID -> codigo e nome em portugues)
"""
import io
import json
import os
import struct

import cairosvg
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FL = os.path.join(HERE, 'flags')

# nomes que o RadioID usa e que diferem do country.json
ALIASES = {
    'United States': 'us', 'USA': 'us', 'United Kingdom': 'gb', 'England': 'gb', 'Scotland': 'gb', 'Wales': 'gb',
    'Northern Ireland': 'gb', 'Russia': 'ru', 'Russian Federation': 'ru', 'Korea Republic of': 'kr', 'South Korea': 'kr',
    'Korea, Republic of': 'kr', 'Taiwan': 'tw', 'Czech Republic': 'cz', 'Czechia': 'cz', 'Iran': 'ir', 'Vietnam': 'vn',
    'Viet Nam': 'vn', 'Venezuela': 've', 'Bolivia': 'bo', 'Moldova': 'md', 'Tanzania': 'tz', 'Syria': 'sy', 'Laos': 'la',
    'Macedonia': 'mk', 'North Macedonia': 'mk', 'Ivory Coast': 'ci', "Cote d'Ivoire": 'ci', 'Brunei': 'bn',
    'Hong Kong': 'hk', 'Macau': 'mo', 'Netherlands Antilles': 'cw', 'Curacao': 'cw', 'Bosnia and Herzegovina': 'ba',
    'Trinidad and Tobago': 'tt', 'Dominican Republic': 'do', 'Puerto Rico': 'pr', 'Turkey': 'tr', 'Turkiye': 'tr',
    'Slovak Republic': 'sk', 'Palestine': 'ps', 'Cape Verde': 'cv', 'Swaziland': 'sz', 'Eswatini': 'sz',
}


# prefixos de indicativo (ITU) -> pais, para quando o cadastro nao traz o pais.
# "PP-PY" expande para PP, PQ, ..., PY; prefixos de 1 letra valem para tudo que comeca com ela.
PREFIXES = """
br PP-PY ZV-ZZ
pt CT CQ-CS CU
es EA-EH AM-AO
us K N W AA-AL
pr KP3 KP4 NP3 NP4 WP3 WP4
ca VA-VG VO VY CF-CK CY CZ
mx XE-XI 4A-4C 6D-6J
ar LO-LW L2-L9 AY AZ
cl CA-CE XQ XR 3G
uy CV-CX
py ZP
bo CP
pe OA-OC
co HJ HK 5J 5K
ve YV-YY 4M
ec HC HD
gb G M 2D 2E 2I 2J 2M 2U 2W
ie EI EJ
de DA-DR
fr F TM
it I
nl PA-PI
be ON-OT
ch HB HE
li HB0
at OE
pl SN-SR 3Z HF
cz OK OL
sk OM
hu HA HG
ro YO-YR
bg LZ
gr SV-SZ J4
tr TA-TC YM
ru R UA-UI
ua UR-UZ EM-EO
by EU-EW
se SA-SM 7S 8S
no LA-LN
dk OU-OZ 5P 5Q
fi OF-OJ
ee ES
lv YL
lt LY
hr 9A
si S5
rs YT YU
ba E7
jp JA-JS 7J-7N 8J-8N
cn B BY BA BD BG BH BI BJ BL BT BZ 3H-3U XS
tw BM-BQ BU-BX
kr HL DS DT 6K-6N
in VU AT-AW 8T-8Y
au VK AX
nz ZL ZM
za ZR-ZU
il 4X 4Z
ph DU-DZ 4D-4I
id YB-YH PK-PO
my 9M 9W
th HS E2
sg 9V S6
hk VR
cu CO CM CL
do HI
lu LX
is TF
cy 5B C4 P3
mt 9H
"""


def prefix_lines():
    out = []
    for line in PREFIXES.strip().splitlines():
        code, *parts = line.split()
        for p in parts:
            if '-' in p:
                a, b = p.split('-')
                for ch in range(ord(a[-1]), ord(b[-1]) + 1):
                    out.append((a[:-1] + chr(ch), code))
            else:
                out.append((p, code))
    out.sort(key=lambda t: -len(t[0]))   # mais longos primeiro: KP4 antes de K
    return '\n'.join('%s %s' % t for t in out) + '\n'


def flag_png(code, w=120, h=90):
    png = cairosvg.svg2png(url=os.path.join(FL, code + '.svg'), output_width=w, output_height=h)
    im = Image.open(io.BytesIO(png)).convert('RGB')
    q = im.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    b = io.BytesIO()
    q.save(b, 'PNG', optimize=True)
    return b.getvalue()


def build():
    from babel import Locale
    pt = Locale('pt', 'BR').territories
    cs = [c for c in json.load(open(os.path.join(FL, 'country.json'))) if c['iso']]
    pngs = []
    for c in cs:
        pngs.append((c['code'], flag_png(c['code'])))
    head = b'FLG1' + struct.pack('<H', len(pngs))
    idx = b''
    off = len(head) + len(pngs) * 8
    data = b''
    for code, p in pngs:
        idx += code.encode()[:2].ljust(2, b'_') + struct.pack('<IH', off + len(data), len(p))
        data += p
    flags = head + idx + data
    lines = []
    seen = set()
    for c in cs:
        name_pt = pt.get(c['code'].upper(), c['name'])
        lines.append('%s|%s|%s' % (c['code'], c['name'], name_pt))
        seen.add(c['name'])
    for en, code in ALIASES.items():
        if en not in seen:
            lines.append('%s|%s|%s' % (code, en, pt.get(code.upper(), en)))
    countries = ('\n'.join(lines) + '\n').encode('utf-8')
    return flags, countries, prefix_lines().encode()


if __name__ == '__main__':
    f, c, p = build()
    print(len(f), len(c), len(p))
    print(p.decode()[:200])
