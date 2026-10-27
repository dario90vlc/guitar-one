import math, random, os, zipfile
# (cairosvg se elimino: el render PNG lo hace _svgrender/render.js con resvg)

W, H = 1200, 540
SANS = "'Barlow Semi Condensed SemiBold', 'Barlow SemiCondensed', 'Helvetica Neue', Helvetica, Arial, sans-serif"
COND = "'Barlow Condensed SemiBold', 'Barlow Condensed', 'Helvetica Neue', Helvetica, Arial, sans-serif"
BLOCK = "'Arial Black', Impact, 'DejaVu Sans', sans-serif"
SERIF = "Georgia, 'DejaVu Serif', serif"
# Fallback del badge del nombre por estilo "logo"; cada amp puede sobreescribirlo con su campo "font"
BLOGO = {
    "script": dict(fam="'Pacifico', 'Segoe Script', Georgia, serif", fs=44, ls=3, fw=400, wf=0.62, title=True),
    "serif": dict(fam=SERIF, fs=42, ls=6, fw=700, wf=0.70),
    "block": dict(fam=BLOCK, fs=40, ls=6, fw=900, wf=0.80),
}


def _lum(c):
    c = c.lstrip("#")
    r, g, b = (int(c[i:i + 2], 16) for i in (0, 2, 4))
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0


def _emb(c):
    # borde de grabado visible segun contraste: texto claro -> luz, texto oscuro -> sombra
    return "#fff" if _lum(c) > 0.5 else "#000"

AMPS = [
 dict(id="01_american_spark", name="SPARKLE TWIN", tag="Limpio brillante · headroom enorme", logo="script",
  font=dict(fam="'Pacifico', 'Segoe Script', Georgia, serif", fs=44, ls=3, fw=400, wf=0.62, title=True, font2="'Helvetica Neue', Helvetica, Arial, sans-serif"),
  body="#151515", dots="#3a3a3a", panel=("#e3e6e9", "#a3a9af"), brushed=True, ptext="#15181b",
  cap=("#3a3a3a", "#050505"), ring=("#f4f4f4", "#8c9096"), ptr="#ffffff", tick="#202428", knob="round",
  ctrl=["VOLUME", "TREBLE", "MIDDLE", "BASS", "REVERB", "SPEED", "INTENSITY"],
  grille="sparkle", g=("#90a5b8", "#e4edf4", "#566c80"), pipe="#c7ccd1", plate=("#0e0e0e", "#0e0e0e"), pt="#e8ecef", jewel="#ff3b2f"),
 dict(id="02_class_a_chime", name="CHIME 30", tag="Chispa grave-media · saturación temprana", logo="serif",
  font=dict(fam="'Poppins SemiBold', 'Poppins', 'Trebuchet MS', sans-serif", fs=42, ls=7, fw=600, wf=0.72, font2="'Helvetica Neue', Helvetica, Arial, sans-serif"),
  body="#1a1714", dots="#3b342c", panel=("#d6a874", "#8b5d33"), brushed=True, ptext="#1f1208",
  cap=("#3b3b3b", "#070707"), ring=("#e8c58f", "#8a6a3b"), ptr="#f5ead3", tick="#2a1a0c", knob="skirt",
  ctrl=["VOLUME", "TREBLE", "BASS", "CUT", "TONE", "MASTER"],
  grille="diamond", g=("#b99d66", "#53421f", "#d8c08a"), pipe="#2a2118", plate=("#201810", "#201810"), pt="#e9c98d", jewel="#ffb02e"),
 dict(id="03_tweed_deluxe", name="AMBER TWEED", tag="Al borde del breakup · madera, sag fuerte", logo="script",
  font=dict(fam="'Pacifico', 'Segoe Script', Georgia, serif", fs=40, ls=4, fw=400, wf=0.62, title=True, font2=COND),
  body="#d6b35e", dots="#8a5a22", bodypat="tweed", panel=("#dfe3e7", "#9aa2aa"), brushed=True, ptext="#22262b",
  cap=("#f2e6c6", "#bfa670"), ring=("#7a5a33", "#3a2713"), ptr="#2a1608", tick="#22262b", knob="chicken",
  ctrl=["MIC VOLUME", "INST VOLUME", "TONE"],
  grille="cross", g=("#6a4424", "#2e1b0c", "#6a4424"), pipe="#3a2713", plate=("#efe2bd", "#efe2bd"), pt="#4a2a12", jewel="#ff4a2e"),
 dict(id="04_spring_clean_120", name="CHORUS 120", tag="Limpio sólido · plano y frío", logo="block",
  font=dict(fam="'Helvetica Neue', Helvetica, Arial, sans-serif", fs=40, ls=6, fw=700, wf=0.74, font2=COND),
  body="#24272b", dots="#3d4247", panel=("#d4d9de", "#9aa1a8"), brushed=True, ptext="#1b1f24",
  cap=("#34383d", "#0d0e10"), ring=("#cfd5da", "#7d848b"), ptr="#ffffff", tick="#1b1f24", knob="round",
  ctrl=["VOLUME", "BASS", "MIDDLE", "TREBLE", "DISTORTION", "REVERB", "RATE", "DEPTH"],
  grille="perf", g=("#14161a", "#34393f", "#14161a"), pipe="#aeb5bb", plate=("#c9d0d6", "#c9d0d6"), pt="#1b1f24", jewel="#45d4ff"),
 dict(id="05_panel_800", name="MARAUDER 800", tag="Crunch británico · medios agresivos", logo="script",
  font=dict(fam="'Lobster Two', 'Yellowtail', 'Segoe Script', cursive", fs=46, ls=2, fw=700, it="italic", wf=0.55, title=True, font2=COND),
  body="#0f0f0f", dots="#2d2d2d", panel=("#1a1a1a", "#090909"), brushed=True, ptext="#e2c882",
  cap=("#2c2c2c", "#050505"), ring=("#e2c882", "#7b6228"), ptr="#ffffff", tick="#d9b45e", knob="skirt",
  ctrl=["PRESENCE", "BASS", "MIDDLE", "TREBLE", "MASTER", "GAIN"],
  grille="cross", g=("#121212", "#2a2a2a", "#121212"), pipe="#d4af52", plate=("#0a0a0a", "#0a0a0a"), pt="#f0d78a", jewel="#ff2a1f"),
 dict(id="06_plexi_glory", name="LEAD 1959", tag="Breakup armónico · graves sueltos", logo="script",
  font=dict(fam="'Lobster Two', 'Yellowtail', 'Segoe Script', cursive", fs=46, ls=2, fw=700, it="italic", wf=0.55, title=True, font2=COND),
  body="#101010", dots="#2d2d2d", panel=("#ecd68e", "#b9993f"), brushed=True, ptext="#0d0b06",
  cap=("#f4efde", "#c6bd9f"), ring=("#d8bf6c", "#7b6228"), ptr="#16120a", tick="#0d0b06", knob="skirt",
  ctrl=["PRESENCE", "BASS", "MIDDLE", "TREBLE", "VOLUME I", "VOLUME II"],
  grille="basket", g=("#1e1d1a", "#37352f", "#1e1d1a"), pipe="#f3edd2", plate=("#0a0a0a", "#0a0a0a"), pt="#f1e4ae", jewel="#ff3322"),
 dict(id="07_blooming_orange", name="TANGERINE 120", tag="Grueso · medios lanudos · fuzz temprano", logo="block", icon="orange",
  font=dict(fam="'Arial Black', Impact, sans-serif", fs=40, ls=5, fw=900, wf=0.80, font2=COND),
  body="#e0701a", dots="#a84a08", panel=("#1b1b1b", "#0b0b0b"), brushed=False, ptext="#f4efe4",
  cap=("#2f2f2f", "#050505"), ring=("#ff8a2a", "#a84a08"), ptr="#ff9a3a", tick="#f4efe4", knob="chunky",
  ctrl=["GAIN", "VOLUME", "TREBLE", "MIDDLE", "BASS", "MASTER"],
  grille="basket", g=("#e8d6a6", "#b39a62", "#e8d6a6"), pipe="#8a3d05", plate=("#f27a12", "#f27a12"), pt="#150a02", jewel="#ff5a1a"),
 dict(id="08_blues_route", name="LONDON BLUES", tag="Tocable · compresión sensible", logo="serif",
  font=dict(fam="'Lobster Two', 'Yellowtail', 'Segoe Script', cursive", fs=44, ls=3, fw=700, it="italic", wf=0.55, title=True, font2=COND),
  body="#b8935a", dots="#7e5f30", panel=("#3a281b", "#1d130b"), brushed=False, wood=True, ptext="#e6c982",
  cap=("#2b2b2b", "#070707"), ring=("#e2c47c", "#8a6c2c"), ptr="#f6e9c3", tick="#e6c982", knob="skirt",
  ctrl=["VOLUME", "TREBLE", "MIDDLE", "BASS", "REVERB", "TREMOLO", "COMP"],
  grille="basket", g=("#cbb27a", "#8d7340", "#cbb27a"), pipe="#4a2f19", plate=("#2a1b10", "#2a1b10"), pt="#ecd18b", jewel="#ffb02e"),
 dict(id="09_studio_hot", name="SESSION HOT", tag="Suave · vocal · boutique", logo="script",
  font=dict(fam="'Prata', 'Bodoni MT', Didot, Georgia, serif", fs=40, ls=8, fw=400, wf=0.70, font2="'Helvetica Neue', Helvetica, Arial, sans-serif"),
  body="#13241f", dots="#264238", panel=("#5a3520", "#2d170a"), brushed=False, wood=True, ptext="#ecd89a",
  cap=("#e2c56f", "#7b5d1d"), ring=("#2a1a0d", "#080402"), ptr="#1a1008", tick="#ecd89a", knob="jewel",
  ctrl=["VOLUME", "BASS", "MIDDLE", "TREBLE", "RATIO", "MASTER"],
  grille="mesh", g=("#1a2f28", "#355a4b", "#1a2f28"), pipe="#d9bd6a", plate=("#d9bd6a", "#d9bd6a"), pt="#1a1008", jewel="#ffc44a"),
 dict(id="10_recto_cali", name="DUO RECTO", tag="V · lows enormes · sizzle", logo="block",
  font=dict(fam="'Michroma', 'Eurostile', 'Bank Gothic', sans-serif", fs=34, ls=4, fw=400, wf=0.95, font2=COND),
  body="#0b0b0c", dots="#292929", panel=("#f0f1f3", "#80848b"), brushed=True, ptext="#0c0c0e",
  cap=("#262626", "#000000"), ring=("#f3f3f5", "#6a6e75"), ptr="#ffffff", tick="#0c0c0e", knob="chunky",
  ctrl=["GAIN", "BASS", "MIDDLE", "TREBLE", "PRESENCE", "MASTER", "OUTPUT"], stripe="#d6171d",
  grille="perf", g=("#0f0f10", "#3a3a3d", "#0f0f10"), pipe="#c5c8cd", plate=("#cfd2d7", "#cfd2d7"), pt="#0c0c0e", jewel="#ff2020"),
 dict(id="11_german_v4", name="KRAFT V4", tag="Moderno · ceñido · articulado", logo="block",
  font=dict(fam="'Archivo Black', 'Anton', sans-serif", fs=38, ls=6, fw=400, wf=0.80, font2=COND),
  body="#1b1d20", dots="#33363a", panel=("#454b52", "#22262a"), brushed=True, ptext="#d8e8f0",
  cap=("#dfe4e8", "#80878e"), ring=("#2a2e33", "#0b0c0d"), ptr="#2a9bff", tick="#8fa9b8", knob="knurl",
  ctrl=["GAIN", "BASS", "MIDDLE", "TREBLE", "PRESENCE", "DEEP", "VOLUME", "MASTER"], stripe="#2a9bff",
  grille="mesh", g=("#101214", "#3a4047", "#101214"), pipe="#59616a", plate=("#0a0b0c", "#0a0b0c"), pt="#7cc4ff", jewel="#2a9bff"),
 # 13 - ampli de PERFILES: carga capturas NAM (.nam) como un ampli mas. Su chapa
 # no imprime el caracter de un ampli concreto porque lo pone la captura: imprime
 # una PANTALLA (el nombre del perfil) y un BOTON de carga.
 dict(id="13_profile", scale_step=5, name="PERFIL", tag="Captura neuronal · carga tu .nam", logo="block", plate_w=330,
  font=dict(fam="'Archivo Black', 'Anton', sans-serif", fs=40, ls=5, fw=400, wf=0.72, font2=COND),
  body="#0a0c0e", dots="#242a30", panel=("#1b2026", "#0b0e11"), brushed=True, ptext="#cfe4f0",
  cap=("#2e343b", "#0d1013"), ring=("#525c66", "#14181c"), ptr="#39e0c0", tick="#8fb6c4", knob="chunky",
  ctrl=["GAIN", "BASS", "MIDDLE", "TREBLE", "VOLUME"], ctrl_x=(672, 976),
  screen=(170, 96, 430, 104), button=(170, 212, 240, 32, "CARGAR PERFIL"),
  stripe="#39e0c0",
  grille="mesh", g=("#0b1013", "#232e35", "#0b1013"), pipe="#8aa2b0", plate=("#0a0e11", "#0a0e11"), pt="#7ef0d0", jewel="#39e0c0"),
 dict(id="12_stage_crush", name="HAVOC 100", tag="Saturado de lead · sustain cantarín", logo="block", plate_w=330,
  font=dict(fam="'Bebas Neue', 'Anton', sans-serif", fs=46, ls=5, fw=400, wf=0.46, font2=COND),
  body="#0c0c0c", dots="#2a2a2a", panel=("#222222", "#0f0f0f"), brushed=False, ptext="#f5f5f5",
  cap=("#fafafa", "#b4b4b4"), ring=("#1a1a1a", "#050505"), ptr="#e31b23", tick="#f5f5f5", knob="chunky",
  ctrl=["PRE GAIN", "POST GAIN", "LOW", "MID", "HIGH", "PRESENCE", "RESONANCE"], stripe="#e31b23",
  grille="cross", g=("#0d0d0d", "#2a2a2a", "#0d0d0d"), pipe="#555", plate=("#ffffff", "#ffffff"), pt="#0c0c0c", jewel="#ff1a1a", platestripe="#e31b23"),
]

# Designacion de modelo serigrafiada en la esquina del panel (autenticidad hardware)
PCODE = {
    "01_american_spark": "TWIN · 85W",
    "02_class_a_chime": "TB-30 · CLASS A",
    "03_tweed_deluxe": "DLX-53 · 15W",
    "04_spring_clean_120": "CHR-120 · SOLID",
    "05_panel_800": "MK-800 · 100W",
    "06_plexi_glory": "SLD-59 · 100W",
    "07_blooming_orange": "TRS-120 · 120W",
    "08_blues_route": "BB-62 · 30W",
    "09_studio_hot": "ODS-001 · 45W",
    "10_recto_cali": "DRC-100 · 2 RECT",
    "11_german_v4": "V4-100 · 4CH",
    "12_stage_crush": "LD-100 · 6L6",
    "13_profile": "NMA-1 · 48 kHz",
}


def patterns(a):
    g1, g2, g3 = a["g"]
    t = a["grille"]
    p = []
    if t == "sparkle":
        # tela tejida base + brillos tipo "sparkle" Fender
        p.append(f'<pattern id="gp" width="14" height="14" patternUnits="userSpaceOnUse">'
                 f'<path d="M0 3.5H14M0 10.5H14" stroke="{g3}" stroke-width="1.8" opacity=".6"/>'
                 f'<path d="M3.5 0V14M10.5 0V14" stroke="{g3}" stroke-width="1.6" opacity=".45"/>'
                 f'<circle cx="3.5" cy="3.5" r="1.5" fill="{g2}"/>'
                 f'<circle cx="10.5" cy="10.5" r="1.8" fill="{g2}" opacity=".9"/>'
                 f'<circle cx="11" cy="3" r="1" fill="#fff" opacity=".95"/>'
                 f'<circle cx="5" cy="11.5" r="1.2" fill="{g2}" opacity=".75"/>'
                 f'<circle cx="7" cy="6.5" r=".7" fill="#fff" opacity=".85"/></pattern>')
    elif t == "diamond":
        # tejido Vox: rombos entrelazados con textil de fondo
        p.append(f'<pattern id="gp" width="30" height="30" patternUnits="userSpaceOnUse">'
                 f'<path d="M0 0H30M0 15H30" stroke="{g3}" stroke-width="1.4" opacity=".45"/>'
                 f'<path d="M0 0V30M15 0V30" stroke="{g3}" stroke-width="1.2" opacity=".3"/>'
                 f'<path d="M15 0L30 15L15 30L0 15Z" fill="none" stroke="{g2}" stroke-width="3"/>'
                 f'<path d="M15 4L26 15L15 26L4 15Z" fill="none" stroke="{g3}" stroke-width="1.6" opacity=".8"/>'
                 f'<circle cx="15" cy="15" r="2.4" fill="{g3}"/>'
                 f'<circle cx="0" cy="0" r="2.2" fill="{g3}"/><circle cx="30" cy="0" r="2.2" fill="{g3}"/>'
                 f'<circle cx="0" cy="30" r="2.2" fill="{g3}"/><circle cx="30" cy="30" r="2.2" fill="{g3}"/></pattern>')
    elif t == "cross":
        # tela cuadriculada tipo Marshall/Orange: hilos con relief
        p.append(f'<pattern id="gp" width="6" height="6" patternUnits="userSpaceOnUse">'
                 f'<path d="M0 0H6" stroke="{g2}" stroke-width="2.2" opacity=".85"/>'
                 f'<path d="M0 0V6" stroke="{g2}" stroke-width="2.2" opacity=".55"/>'
                 f'<path d="M0 3H6" stroke="#000" stroke-width=".9" opacity=".35"/>'
                 f'<path d="M3 0V6" stroke="#000" stroke-width=".9" opacity=".3"/>'
                 f'<path d="M0 1.1H6" stroke="{g3}" stroke-width=".7" opacity=".5"/></pattern>')
    elif t == "basket":
        # tejido cesto clásico con pasadores de hilo
        p.append(f'<pattern id="gp" width="24" height="24" patternUnits="userSpaceOnUse">'
                 f'<rect x="0" y="0" width="12" height="12" fill="{g2}" opacity=".55"/>'
                 f'<rect x="12" y="12" width="12" height="12" fill="{g2}" opacity=".55"/>'
                 f'<path d="M0 12H24M12 0V24" stroke="#000" stroke-opacity=".3" stroke-width="1.6"/>'
                 f'<path d="M0 4H12M0 8H12M12 16H24M12 20H24" stroke="#000" stroke-opacity=".16" stroke-width="1"/>'
                 f'<path d="M0 4.7H12M12 16.7H24" stroke="{g3}" stroke-opacity=".4" stroke-width=".8"/>'
                 f'<path d="M4 0V12M16 12V24" stroke="#000" stroke-opacity=".1" stroke-width="1.4"/></pattern>')
    elif t == "perf":
        # metal perforado: agujero con sombra interna + bisel
        p.append(f'<pattern id="gp" width="12" height="12" patternUnits="userSpaceOnUse">'
                 f'<circle cx="6" cy="6" r="4.6" fill="none" stroke="{g2}" stroke-width=".8" opacity=".6"/>'
                 f'<circle cx="6" cy="6.5" r="3.3" fill="#000" opacity=".92"/>'
                 f'<path d="M2.9 5.6a3.3 3.3 0 0 1 6.2 0" fill="none" stroke="{g2}" stroke-width="1.3" opacity=".9"/>'
                 f'<circle cx="6" cy="6" r="3.3" fill="none" stroke="#000" stroke-opacity=".5" stroke-width=".7"/></pattern>')
    elif t == "mesh":
        # malla expandida: hilos cruzados con brillo de alambre
        p.append(f'<pattern id="gp" width="10" height="10" patternUnits="userSpaceOnUse">'
                 f'<path d="M0 0L10 10" stroke="{g2}" stroke-width="2"/>'
                 f'<path d="M10 0L0 10" stroke="{g2}" stroke-width="1.5" opacity=".75"/>'
                 f'<path d="M0 0L10 10" stroke="#fff" stroke-width=".6" opacity=".25"/>'
                 f'<path d="M5 0V10M0 5H10" stroke="{g3}" stroke-width=".8" opacity=".4"/>'
                 f'<rect width="10" height="10" fill="none" stroke="#000" stroke-opacity=".2" stroke-width="1"/></pattern>')
    if a.get("bodypat") == "tweed":
        # tweed Fender: diagonal de dos tonos + trama fina
        p.append('<pattern id="bp" width="14" height="14" patternUnits="userSpaceOnUse">'
                 '<path d="M-4 4L4 -4M0 14L14 0M10 18L18 10" stroke="#f2d994" stroke-width="3.2" opacity=".7"/>'
                 '<path d="M-4 11L11 -4M3 18L18 3" stroke="#a8752e" stroke-width="2.6" opacity=".55"/>'
                 '<path d="M-4 7.5L7.5 -4M6.5 18L18 6.5" stroke="#6e4218" stroke-width="1.3" opacity=".5"/>'
                 '</pattern>')
    else:
        d = a["dots"]
        # tolex granulado: grano irregular + brillo sutil
        p.append(f'<pattern id="bp" width="7" height="7" patternUnits="userSpaceOnUse"><circle cx="1.5" cy="1.5" r="1.15" fill="{d}"/><circle cx="5" cy="4.2" r=".95" fill="{d}" opacity=".8"/><circle cx="3.2" cy="6.2" r=".7" fill="{d}" opacity=".6"/><circle cx="1.2" cy="1.1" r=".4" fill="#fff" opacity=".07"/></pattern>')
    return "".join(p)


def ctrl_geo(a):
    """Posiciones y radio de las perillas. La MISMA cuenta manda en el fondo y
    en la tira, o el hueco impreso y la perilla que pinta la app no casan.
    Las posiciones (x) no se tocan: van clavadas en amp_frontales_data.h."""
    n = len(a["ctrl"])
    x0, x1 = a.get("ctrl_x") or ((200, 1000) if n >= 5 else (330, 870))
    if n <= 3 and not a.get("ctrl_x"): x0, x1 = 380, 820
    xs = [x0 + (x1 - x0) * i / (n - 1) for i in range(n)]
    sp = (x1 - x0) / (n - 1) if n > 1 else 400.0
    # en un hueco apretado la perilla y su escala encogen con la separacion
    kr = 27.0 if sp >= 104 else 27.0 * sp / 104.0
    return xs, sp, kr


def knob(x, y, r, ang, a, mode="all"):
    k = a["knob"]
    o = []
    r0 = r * 1.22 + 5
    lng, sht = (8, 5) if r > 24 else (7, 4)
    for i in range(11 if mode != "body" else 0):
        t = math.radians(-135 + 27 * i)
        r2 = r0 + (lng if i % 5 == 0 else sht)
        o.append(f'<line x1="{x+r0*math.sin(t):.1f}" y1="{y-r0*math.cos(t):.1f}" x2="{x+r2*math.sin(t):.1f}" y2="{y-r2*math.cos(t):.1f}" stroke="{a["tick"]}" stroke-width="{2 if i%5==0 else 1.3}" stroke-linecap="round" opacity=".8"/>')
    # escala numerica 0-10 en el panel (Fender/Marshall/Mesa). El radio lo fija
    # build() para que las escalas de dos perillas vecinas no se pisen; si ni
    # asi cabe (_num = False), la escala se omite.
    if mode != "body" and k in ("round", "skirt", "chunky") and a.get("_num", True):
        step = a.get("scale_step", 1)
        rn = a.get("_numr", r * 1.22 + 23)
        fs = a.get("_numfs", 10)
        for i in range(0, 11, step):
            t = math.radians(-135 + 27 * i)
            o.append(f'<text x="{x+rn*math.sin(t):.1f}" y="{y-rn*math.cos(t)+fs*0.44:.1f}" text-anchor="middle" font-family="{SANS}" font-size="{fs}" font-weight="700" fill="{_emb(a["tick"])}" opacity=".36">{i}</text>')
            o.append(f'<text x="{x+rn*math.sin(t):.1f}" y="{y-rn*math.cos(t)+fs*0.34:.1f}" text-anchor="middle" font-family="{SANS}" font-size="{fs}" font-weight="700" fill="{a["tick"]}" opacity=".8">{i}</text>')
    if mode == "ticks":
        return "".join(o)
    # sombra de contacto: la perilla se apoya en la chapa. Antes era una elipse
    # dura de un paso; ahora es una sombra blanda de tres, que es lo que la
    # asienta en el panel en vez de dejarla flotando.
    o.append(f'<ellipse cx="{x+2}" cy="{y+7}" rx="{r*1.36:.1f}" ry="{r*1.26:.1f}" fill="#000" opacity=".14"/>')
    o.append(f'<ellipse cx="{x+2}" cy="{y+5}" rx="{r*1.27:.1f}" ry="{r*1.19:.1f}" fill="#000" opacity=".16"/>')
    o.append(f'<ellipse cx="{x+1}" cy="{y+3}" rx="{r*1.17:.1f}" ry="{r*1.11:.1f}" fill="#000" opacity=".18"/>')
    rr = r * (1.3 if k in ("chunky", "skirt", "knurl") else 1.18)
    o.append(f'<circle cx="{x}" cy="{y}" r="{rr:.1f}" fill="url(#ring)" stroke="#000" stroke-opacity=".5" stroke-width="1"/>')
    # filo del aro: una sola luz, desde arriba; la sombra cae abajo
    o.append(f'<path d="M{x-rr*0.98:.1f} {y:.1f} A{rr*0.98:.1f} {rr*0.98:.1f} 0 0 1 {x+rr*0.98:.1f} {y:.1f}" fill="none" stroke="#fff" stroke-opacity=".15" stroke-width="{max(1.0, r*0.07):.1f}"/>')
    o.append(f'<path d="M{x-rr*0.94:.1f} {y:.1f} A{rr*0.94:.1f} {rr*0.94:.1f} 0 0 0 {x+rr*0.94:.1f} {y:.1f}" fill="none" stroke="#000" stroke-opacity=".26" stroke-width="{max(1.0, r*0.07):.1f}"/>')
    if k == "knurl":
        for i in range(36):
            t = math.radians(i * 10)
            o.append(f'<line x1="{x+r*1.02*math.sin(t):.1f}" y1="{y-r*1.02*math.cos(t):.1f}" x2="{x+rr*math.sin(t):.1f}" y2="{y-rr*math.cos(t):.1f}" stroke="#000" stroke-opacity=".55" stroke-width="1.4"/>')
        for i in range(36):
            t = math.radians(i * 10 + 5)
            o.append(f'<line x1="{x+r*1.06*math.sin(t):.1f}" y1="{y-r*1.06*math.cos(t):.1f}" x2="{x+(rr-1.5)*math.sin(t):.1f}" y2="{y-(rr-1.5)*math.cos(t):.1f}" stroke="#fff" stroke-opacity=".18" stroke-width=".8"/>')
    if k in ("skirt", "round", "chunky"):
        # falda estriada (ranuras de agarre) alrededor del cuerpo
        for i in range(20):
            t = math.radians(i * 18)
            o.append(f'<line x1="{x+(rr-4.5)*math.sin(t):.1f}" y1="{y-(rr-4.5)*math.cos(t):.1f}" x2="{x+(rr-1)*math.sin(t):.1f}" y2="{y-(rr-1)*math.cos(t):.1f}" stroke="#000" stroke-opacity=".4" stroke-width="1.6" stroke-linecap="round"/>')
        for i in range(20):
            t = math.radians(i * 18 + 9)
            o.append(f'<line x1="{x+(rr-4.5)*math.sin(t):.1f}" y1="{y-(rr-4.5)*math.cos(t):.1f}" x2="{x+(rr-2.2)*math.sin(t):.1f}" y2="{y-(rr-2.2)*math.cos(t):.1f}" stroke="#fff" stroke-opacity=".15" stroke-width=".9"/>')
    if k == "chicken":
        o.append(f'<g transform="rotate({ang} {x} {y})"><path d="M{x-r*.55} {y} L{x-r*.2} {y-r*1.55} Q{x} {y-r*1.7} {x+r*.2} {y-r*1.55} L{x+r*.55} {y} Z" fill="url(#cap)" stroke="#000" stroke-opacity=".5"/></g>')
        # pico de gallina: borde superior con brillo
        o.append(f'<g transform="rotate({ang} {x} {y})"><path d="M{x-r*.2} {y-r*1.55} Q{x} {y-r*1.7} {x+r*.2} {y-r*1.55}" fill="none" stroke="#fff" stroke-opacity=".35" stroke-width="1.6"/></g>')
    o.append(f'<circle cx="{x}" cy="{y}" r="{r*(0.82 if k!="round" else 0.9):.1f}" fill="url(#cap)" stroke="#000" stroke-opacity=".55" stroke-width="1"/>')
    # aro interior del cap (remache/cosmetica)
    o.append(f'<circle cx="{x}" cy="{y}" r="{r*(0.62 if k!="round" else 0.7):.1f}" fill="none" stroke="#000" stroke-opacity=".22" stroke-width="1.2"/>')
    if k == "jewel":
        o.append(f'<circle cx="{x}" cy="{y}" r="{r*.55:.1f}" fill="none" stroke="#000" stroke-opacity=".35" stroke-width="1.5"/>')
    o.append(f'<ellipse cx="{x-r*.25:.1f}" cy="{y-r*.4:.1f}" rx="{r*.42:.1f}" ry="{r*.20:.1f}" fill="#fff" opacity=".20" transform="rotate(-25 {x} {y})"/>')
    if k == "chicken":
        o.append(f'<g transform="rotate({ang} {x} {y})"><line x1="{x}" y1="{y-r*.3:.1f}" x2="{x}" y2="{y-r*1.4:.1f}" stroke="#000" stroke-opacity=".5" stroke-width="6" stroke-linecap="round"/><line x1="{x}" y1="{y-r*.3:.1f}" x2="{x}" y2="{y-r*1.4:.1f}" stroke="{a["ptr"]}" stroke-width="3.5" stroke-linecap="round"/></g>')
    else:
        o.append(f'<g transform="rotate({ang} {x} {y})"><line x1="{x}" y1="{y-r*.3:.1f}" x2="{x}" y2="{y-r*.82:.1f}" stroke="#000" stroke-opacity=".5" stroke-width="7" stroke-linecap="round"/><line x1="{x}" y1="{y-r*.3:.1f}" x2="{x}" y2="{y-r*.82:.1f}" stroke="{a["ptr"]}" stroke-width="4" stroke-linecap="round"/></g>')
    return "".join(o)


def screw(x, y, c):
    return f'<circle cx="{x}" cy="{y}" r="6" fill="url(#ring)" stroke="#000" stroke-opacity=".6"/><path d="M{x-4} {y-1}L{x+4} {y+1}" stroke="#000" stroke-opacity=".6" stroke-width="1.6"/>'


def build(a, knobs=True):
    rnd = random.Random(a["id"])
    g1 = a["g"][0]
    px, py, pw, ph = 60, 50, 1080, 190
    gx, gy, gw, gh = 60, 262, 1080, 220
    d = []
    d.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
    d.append("<defs>")
    d.append(f'<linearGradient id="panel" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{a["panel"][0]}"/><stop offset="1" stop-color="{a["panel"][1]}"/></linearGradient>')
    d.append(f'<radialGradient id="cap" cx=".35" cy=".3" r=".9"><stop offset="0" stop-color="{a["cap"][0]}"/><stop offset="1" stop-color="{a["cap"][1]}"/></radialGradient>')
    d.append(f'<linearGradient id="ring" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{a["ring"][0]}"/><stop offset=".5" stop-color="{a["ring"][1]}"/><stop offset="1" stop-color="{a["ring"][0]}"/></linearGradient>')
    d.append(f'<radialGradient id="jw" cx=".5" cy=".5" r=".5"><stop offset="0" stop-color="{a["jewel"]}" stop-opacity=".9"/><stop offset="1" stop-color="{a["jewel"]}" stop-opacity="0"/></radialGradient>')
    d.append(f'<radialGradient id="jg" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#fff"/><stop offset=".35" stop-color="{a["jewel"]}"/><stop offset="1" stop-color="#000" stop-opacity=".7"/></radialGradient>')
    # pantalla del ampli de perfiles: fosforo verde-azulado, apagado
    d.append('<linearGradient id="scr" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0a1a18"/><stop offset=".5" stop-color="#06100f"/><stop offset="1" stop-color="#081513"/></linearGradient>')
    # --- luz de estudio: clave calida arriba-izquierda, relleno frio a la derecha
    _sd = rnd.randint(1, 9000)
    d.append('<linearGradient id="key" x1="0" y1="0" x2="1" y2="1">'
             '<stop offset="0" stop-color="#fff" stop-opacity=".13"/>'
             '<stop offset=".34" stop-color="#fff" stop-opacity=".03"/>'
             '<stop offset=".62" stop-color="#000" stop-opacity=".10"/>'
             '<stop offset="1" stop-color="#000" stop-opacity=".42"/></linearGradient>')
    # micro-grano de material: fino y a dosis minima. Antes era un velo de 4
    # octavas a .30 sobre TODO (cuerpo, chapa, tela): el ojo lo leia como una
    # neblina uniforme que ensuciaba el conjunto. Ahora es textura, no velo.
    d.append(f'<filter id="grain" x="-2%" y="-2%" width="104%" height="104%">'
             f'<feTurbulence type="fractalNoise" baseFrequency="1.4 1.7" numOctaves="2" seed="{_sd}" stitchTiles="stitch"/>'
             f'<feColorMatrix type="matrix" values="0 0 0 0 .5  0 0 0 0 .5  0 0 0 0 .5  0.55 0 0 0 -.24"/></filter>')
    # cepillado: ruido estirado en horizontal -> rayas largas de metal
    d.append(f'<filter id="brushedF" x="-2%" y="-2%" width="104%" height="104%">'
             f'<feTurbulence type="fractalNoise" baseFrequency="0.0025 0.9" numOctaves="2" seed="{_sd + 31}" stitchTiles="stitch"/>'
             f'<feColorMatrix type="matrix" values="0 0 0 0 .5  0 0 0 0 .5  0 0 0 0 .5  0.9 0 0 0 -.34"/></filter>')
    # tela: ruido fino de dos octavas, lee como tejido, no como patron impreso
    d.append(f'<filter id="clothF" x="-2%" y="-2%" width="104%" height="104%">'
             f'<feTurbulence type="fractalNoise" baseFrequency="1.1 0.55" numOctaves="3" seed="{_sd + 77}" stitchTiles="stitch"/>'
             f'<feColorMatrix type="matrix" values="0 0 0 0 .5  0 0 0 0 .5  0 0 0 0 .5  0.95 0 0 0 -.32"/></filter>')
    # mancha grande de tejido: es lo que mata el "tiling" (la repeticion del
    # patron cada 6..30 px era lo que delataba que la tela era un sello).
    d.append(f'<filter id="clothL" x="-2%" y="-2%" width="104%" height="104%">'
             f'<feTurbulence type="fractalNoise" baseFrequency="0.006 0.009" numOctaves="2" seed="{_sd + 53}" stitchTiles="stitch"/>'
             f'<feColorMatrix type="matrix" values="0 0 0 0 .5  0 0 0 0 .5  0 0 0 0 .5  0.75 0 0 0 -.34"/></filter>')
    # (retirado) clip del mueble y degradados de esquinera: sin esquineras no
    #  hacen falta; las cuatro esquinas quedan con el canto redondeado del cuerpo.
    d.append('<linearGradient id="lip" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#000" stop-opacity=".5"/><stop offset="1" stop-color="#000" stop-opacity="0"/></linearGradient>')
    d.append('<linearGradient id="gloss" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".28"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/></linearGradient>')
    d.append(f'<linearGradient id="plate" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{a["plate"][0]}"/><stop offset="1" stop-color="{a["plate"][1]}"/></linearGradient>')
    # luz de la tela: el bafle esta curvado, asi que la tela coge luz arriba y
    # cae a sombra en los cantos. Esto es lo que le da profundidad al hueco.
    d.append('<radialGradient id="baf" cx=".5" cy=".34" r=".82">'
             '<stop offset="0" stop-color="#fff" stop-opacity=".10"/>'
             '<stop offset=".55" stop-color="#000" stop-opacity="0"/>'
             '<stop offset="1" stop-color="#000" stop-opacity=".34"/></radialGradient>')
    d.append('<filter id="sh" x="-20%" y="-20%" width="140%" height="160%"><feGaussianBlur stdDeviation="6"/></filter>')
    d.append(patterns(a))
    d.append("</defs>")
    # body: llena TODO el lienzo (sin marco negro alrededor); las esquinas
    # redondeadas quedan transparentes para que se vea el fondo de la UI.
    d.append(f'<rect x="0" y="0" width="1200" height="540" rx="18" fill="{a["body"]}"/>')
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="url(#bp)"/>')
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="url(#gloss)" opacity=".30"/>')
    # material del cuerpo: cepillado + micro-grano (dosis minima) + la luz
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="#808080" filter="url(#brushedF)" opacity=".14"/>')
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="#808080" filter="url(#grain)" opacity=".06"/>')
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="url(#key)"/>')
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="none" stroke="#000" stroke-opacity=".55" stroke-width="3"/>')
    # (retirado) Aqui iban cuatro esquineras: una tapa de 92px pintada en cada
    #  esquina del mueble. Sobre un cuerpo oscuro la tapa (#131313) dibujaba una
    #  muesca negra; con luz de metal, una mancha clara en la punta. En los dos
    #  casos el canto superior salia roto en las esquinas, asi que el canto queda
    #  limpio: manda la silueta redondeada del propio mueble.
    # panel
    d.append(f'<rect x="{px+4}" y="{py+8}" width="{pw}" height="{ph}" rx="8" fill="#000" opacity=".55" filter="url(#sh)"/>')
    d.append(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="8" fill="url(#panel)" stroke="#000" stroke-opacity=".7" stroke-width="2"/>')
    if a.get("brushed"):
        for _ in range(110):
            yy = rnd.uniform(py + 3, py + ph - 3)
            c = "#fff" if rnd.random() < .5 else "#000"
            d.append(f'<line x1="{px+3}" y1="{yy:.1f}" x2="{px+pw-3}" y2="{yy:.1f}" stroke="{c}" stroke-opacity="{rnd.uniform(.03,.09):.3f}" stroke-width="{rnd.uniform(.5,1.4):.1f}"/>')
    if a.get("wood"):
        for _ in range(46):
            yy = rnd.uniform(py + 4, py + ph - 4)
            c = "#000" if rnd.random() < .6 else "#c98a4a"
            amp = rnd.uniform(-5, 5)
            d.append(f'<path d="M{px+3} {yy:.1f} C{px+300} {yy+amp:.1f} {px+700} {yy-amp:.1f} {px+pw-3} {yy+amp*.4:.1f}" fill="none" stroke="{c}" stroke-opacity="{rnd.uniform(.08,.22):.2f}" stroke-width="{rnd.uniform(.8,2.2):.1f}"/>')
    # bisel de la chapa: filo claro arriba-izquierda y oscuro abajo-derecha.
    #  Antes era un rectangulo a 8px del borde (doble marco flotante).
    d.append(f'<path d="M{px+2.5} {py+ph-8} V{py+2.5} H{px+pw-8}" fill="none" stroke="#fff" stroke-opacity=".30" stroke-width="2"/>')
    d.append(f'<path d="M{px+8} {py+ph-2.5} H{px+pw-2.5} V{py+8}" fill="none" stroke="#000" stroke-opacity=".42" stroke-width="2"/>')
    if a.get("stripe"):
        d.append(f'<rect x="{px+8}" y="{py+ph-24}" width="{pw-16}" height="6" fill="{a["stripe"]}"/>')
    for sx, sy in [(px + 22, py + 22), (px + pw - 22, py + 22), (px + 22, py + ph - 22), (px + pw - 22, py + ph - 22)]:
        d.append(screw(sx, sy, a["ptext"]))
    pc = PCODE.get(a["id"])
    if pc:
        # el codigo de modelo arranca despues del tornillo de la esquina (antes
        # nacia debajo y el tornillo se comia la primera letra)
        d.append(f'<text x="{px+42}" y="{py+20}" font-family="{SANS}" font-size="11" font-weight="700" letter-spacing="2.2" fill="{a["ptext"]}" opacity=".6">{pc}</text>')
    # chapa del panel: cepillado metalico + grano fino + brillo direccional
    d.append(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="8" fill="#808080" filter="url(#brushedF)" opacity="{.30 if a.get("brushed") else .12}"/>')
    d.append(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="8" fill="#808080" filter="url(#grain)" opacity=".05"/>')
    d.append(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="8" fill="url(#gloss)" opacity=".26"/>')
    d.append(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="8" fill="url(#key)" opacity=".42"/>')
    d.append(f'<line x1="{px+8}" y1="{py+3}" x2="{px+pw-8}" y2="{py+3}" stroke="#fff" stroke-opacity=".34" stroke-width="1.6"/>')
    d.append(f'<line x1="{px+8}" y1="{py+ph-2.5}" x2="{px+pw-8}" y2="{py+ph-2.5}" stroke="#000" stroke-opacity=".45" stroke-width="2"/>')
    # jack
    d.append(f'<circle cx="112" cy="160" r="17" fill="url(#ring)" stroke="#000" stroke-width="1.5"/><circle cx="112" cy="160" r="8" fill="#050505"/><circle cx="112" cy="160" r="3.5" fill="#777"/>')
    d.append(f'<text x="112" y="117" text-anchor="middle" font-family="{SANS}" font-size="12" font-weight="700" letter-spacing="1.5" fill="{_emb(a["ptext"])}" opacity=".45">INPUT</text>')
    d.append(f'<text x="112" y="116" text-anchor="middle" font-family="{SANS}" font-size="12" font-weight="700" letter-spacing="1.5" fill="{a["ptext"]}">INPUT</text>')
    # PANTALLA: bisel hundido + display oscuro con rejilla tenue. Es donde la app
    # imprime el nombre del perfil cargado (el frontal la deja preparada).
    if a.get("screen"):
        sx, sy, sw, sh = a["screen"]
        d.append(f'<rect x="{sx-7}" y="{sy-7}" width="{sw+14}" height="{sh+14}" rx="11" fill="#07080a" stroke="#000" stroke-width="2"/>')
        d.append(f'<rect x="{sx-6}" y="{sy-6}" width="{sw+12}" height="{sh+12}" rx="10" fill="none" stroke="#fff" stroke-opacity=".14" stroke-width="1"/>')
        d.append(f'<rect x="{sx}" y="{sy}" width="{sw}" height="{sh}" rx="6" fill="url(#scr)"/>')
        for i in range(1, 8):
            gx2 = sx + sw*i/8.0
            d.append(f'<line x1="{gx2:.1f}" y1="{sy+5}" x2="{gx2:.1f}" y2="{sy+sh-5}" stroke="#7ef0d0" stroke-opacity=".06" stroke-width="1"/>')
        d.append(f'<line x1="{sx+6}" y1="{sy+sh-14}" x2="{sx+sw-6}" y2="{sy+sh-14}" stroke="#7ef0d0" stroke-opacity=".10" stroke-width="1"/>')
        d.append(f'<rect x="{sx}" y="{sy}" width="{sw}" height="{sh*0.42:.0f}" rx="6" fill="#fff" opacity=".025"/>')
        # (el rotulo de la pantalla lo escribe la app: el perfil cargado)

    # BOTON impreso (la app pinta encima su hover y su pulsacion)
    if a.get("button"):
        bx, by, bw, bh, blab = a["button"]
        d.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="6" fill="#1a1d21" stroke="#000" stroke-width="1.6"/>')
        d.append(f'<rect x="{bx+1.5}" y="{by+1.5}" width="{bw-3}" height="{bh-3}" rx="5" fill="none" stroke="#fff" stroke-opacity=".11"/>')
        d.append(f'<text x="{bx+bw/2}" y="{by+bh/2+4}" text-anchor="middle" font-family="{SANS}" font-size="12" font-weight="700" letter-spacing="1.6" fill="{a["pt"]}">{blab}</text>')

    # knobs
    pos = []
    a["_pos"] = pos
    xs, sp, kr = ctrl_geo(a)
    # La escala numerica se pega al borde del hueco: el radio encoge hasta que
    # los numeros de los extremos (i=2 e i=8, los que mas se acercan al vecino)
    # dejan 9 px entre si. Si al encoger entra dentro de las marcas, se omite
    # (el de perfiles mete cinco perillas en 304 px: ahi no hay escala).
    rn = min(kr * 1.22 + 23, (sp - 14) / (2 * math.sin(math.radians(81))))
    a["_numr"] = rn
    a["_numfs"] = 10 if rn >= kr * 1.22 + 23 else 9
    a["_num"] = rn >= kr * 1.22 + 16
    for x, lab in zip(xs, a["ctrl"]):
        ang = rnd.randint(-100, 100)
        # el rotulo encoge con el hueco: "MIDDLE" y "TREBLE" se tocaban
        fs = 14 if len(lab) < 9 else 12
        while fs > 8 and len(lab) * (fs * 0.74 + 1.5) > sp - 10: fs -= 1
        ls = 1.5 if sp >= 110 else 1.0
        d.append(f'<text x="{x:.1f}" y="89" text-anchor="middle" font-family="{SANS}" font-size="{fs}" font-weight="800" letter-spacing="{ls}" fill="{_emb(a["ptext"])}" opacity=".40">{lab}</text>')
        d.append(f'<text x="{x:.1f}" y="88" text-anchor="middle" font-family="{SANS}" font-size="{fs}" font-weight="800" letter-spacing="{ls}" fill="{a["ptext"]}">{lab}</text>')
        d.append(f'<line x1="{x-11:.0f}" y1="94" x2="{x+11:.0f}" y2="94" stroke="{a["ptext"]}" stroke-width="1.6" opacity=".42" stroke-linecap="round"/>')
        d.append(knob(round(x, 1), 164, kr, ang, a, "all" if knobs else "ticks"))
        pos.append(dict(label=lab, x=round(x, 1), y=164))
    # jewel + switch. El grupo va pegado al canto derecho: en los frontales de
    # siete u ocho mandos la ultima escala numerica quedaba tocando "POWER".
    _jx = 1098
    d.append(f'<circle cx="{_jx}" cy="120" r="46" fill="url(#jw)" opacity=".20"/>')
    d.append(f'<circle cx="{_jx}" cy="120" r="34" fill="url(#jw)"/>')
    d.append(f'<circle cx="{_jx}" cy="120" r="18" fill="none" stroke="#8f959b" stroke-width="3" opacity=".85"/>')
    d.append(f'<circle cx="{_jx}" cy="120" r="14" fill="url(#jg)" stroke="#000" stroke-width="2"/>')
    d.append(f'<circle cx="{_jx-5}" cy="115" r="4.5" fill="#fff" opacity=".8"/>')
    d.append(f'<text x="{_jx}" y="163" text-anchor="middle" font-family="{SANS}" font-size="10" font-weight="800" letter-spacing="1.3" fill="{_emb(a["ptext"])}" opacity=".40">POWER</text>')
    d.append(f'<text x="{_jx}" y="162" text-anchor="middle" font-family="{SANS}" font-size="10" font-weight="800" letter-spacing="1.3" fill="{a["ptext"]}">POWER</text>')
    d.append(f'<rect x="{_jx-11}" y="172" width="22" height="30" rx="5" fill="#111" stroke="#000"/><rect x="{_jx-8}" y="175" width="16" height="13" rx="3" fill="url(#ring)"/>')
    # --- TELA DE LA REJILLA --------------------------------------------------
    #  Orden: base -> perfil del cono -> tejido -> mancha grande (mata la
    #  repeticion del patron, que era lo que delataba que la tela era un sello)
    #  -> relieve de los altavoces POR ENCIMA del tejido -> una sola luz.
    d.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="6" fill="{g1}"/>')
    _cy = gy + gh//2
    _spk = (gx + gw * 0.28, gx + gw * 0.72)
    for _sx in _spk:
        # perfil del cono: campana al centro, cono que coge luz, canto en sombra.
        # El brillo del cono es luz propia (blanco a poca dosis), no el color del
        # tejido: en los amplis oscuros el gris de la tela dejaba el altavoz negro.
        d.append(f'<radialGradient id="cn{int(_sx)}" cx=".5" cy=".5" r=".5">'
                 f'<stop offset="0" stop-color="#000" stop-opacity=".46"/>'
                 f'<stop offset=".24" stop-color="#000" stop-opacity=".12"/>'
                 f'<stop offset=".52" stop-color="#fff" stop-opacity=".11"/>'
                 f'<stop offset=".82" stop-color="#000" stop-opacity=".30"/>'
                 f'<stop offset="1" stop-color="#000" stop-opacity="0"/></radialGradient>')
        d.append(f'<circle cx="{_sx:.0f}" cy="{_cy}" r="98" fill="url(#cn{int(_sx)})"/>')
    d.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="6" fill="url(#gp)"/>')
    d.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="6" fill="#808080" filter="url(#clothF)" opacity=".30"/>')
    d.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="6" fill="#808080" filter="url(#clothL)" opacity=".18"/>')
    # el altavoz empuja la tela: relieve por encima del tejido
    for _sx in _spk:
        d.append(f'<circle cx="{_sx:.0f}" cy="{_cy}" r="94" fill="none" stroke="#000" stroke-opacity=".34" stroke-width="11"/>')
        d.append(f'<path d="M{_sx-90:.0f} {_cy-24:.0f}a90 90 0 0 1 120 -36" fill="none" stroke="#fff" stroke-opacity=".13" stroke-width="5"/>')
        d.append(f'<circle cx="{_sx:.0f}" cy="{_cy}" r="62" fill="none" stroke="#000" stroke-opacity=".20" stroke-width="3"/>')
        d.append(f'<circle cx="{_sx:.0f}" cy="{_cy}" r="35" fill="#000" opacity=".30"/>')
        d.append(f'<circle cx="{_sx:.0f}" cy="{_cy}" r="35" fill="none" stroke="#000" stroke-opacity=".38" stroke-width="2"/>')
        d.append(f'<path d="M{_sx-30:.0f} {_cy-14:.0f}a30 30 0 0 1 44 -8" fill="none" stroke="#fff" stroke-opacity=".10" stroke-width="3"/>')
    # luz del bafle: el hueco esta curvado, coge luz arriba y cae en los cantos
    d.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="6" fill="url(#baf)"/>')
    d.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="6" fill="url(#key)" opacity=".45"/>')
    d.append(f'<rect x="{gx+4}" y="{gy+4}" width="{gw-8}" height="20" fill="url(#lip)"/>')
    d.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="6" fill="none" stroke="{a["pipe"]}" stroke-width="5"/>')
    # filo de luz en el canto interior del marco de la tela (bisel del baffle)
    d.append(f'<path d="M{gx+3} {gy+gh-4} V{gy+3} H{gx+gw-3}" fill="none" stroke="#fff" stroke-opacity=".22" stroke-width="2"/>')
    d.append(f'<path d="M{gx+4} {gy+gh-3} H{gx+gw-4} V{gy+4}" fill="none" stroke="#000" stroke-opacity=".35" stroke-width="2"/>')
    # badge - tipografia por amp (evoca el modelo original; campo "font" de cada amp o BLOGO)
    name = a["name"]
    f = {**BLOGO[a["logo"]], **(a.get("font") or {})}
    fs, ls, fam, wf = f["fs"], f["ls"], f["fam"], f["wf"]
    style = f'font-style="{f["it"]}" font-weight="{f["fw"]}"' if f.get("it") else f'font-weight="{f["fw"]}"'
    label = name.title() if f.get("title") else name
    tw = len(label) * (fs * wf + ls) + 70
    if a.get("icon"): tw += 60
    if a.get("plate_w"): tw = max(tw, a["plate_w"])
    # la chapa tambien tiene que dar cabida a la linea de caracter: en el tweed
    # la etiqueta larga metia el texto debajo de los dos remaches del canto
    tw = max(tw, len(a["tag"]) * (12 * 0.68 + 1.2) + 60)

    cx, cy = 600, 400
    d.append(f'<rect x="{cx-tw/2+4:.0f}" y="{cy-40+8}" width="{tw:.0f}" height="84" rx="10" fill="#000" opacity=".5" filter="url(#sh)"/>')
    d.append(f'<rect x="{cx-tw/2:.0f}" y="{cy-40}" width="{tw:.0f}" height="84" rx="10" fill="url(#plate)" stroke="#000" stroke-width="2"/>')
    d.append(f'<rect x="{cx-tw/2+5:.0f}" y="{cy-35}" width="{tw-10:.0f}" height="74" rx="7" fill="none" stroke="{a["pt"]}" stroke-opacity=".45" stroke-width="1.4"/>')
    for _rx in (cx - tw/2 + 14, cx + tw/2 - 14):
        for _ry in (cy - 30, cy + 30):
            d.append(f'<circle cx="{_rx:.0f}" cy="{_ry:.0f}" r="3.4" fill="{a["pt"]}" opacity=".5"/>')
            d.append(f'<circle cx="{_rx:.0f}" cy="{_ry:.0f}" r="3.4" fill="none" stroke="#000" stroke-opacity=".45"/>')
    d.append(f'<rect x="{cx-tw/2:.0f}" y="{cy-40}" width="{tw:.0f}" height="84" rx="10" fill="url(#gloss)" opacity=".4"/>')
    tx = cx + (28 if a.get("icon") else 0)
    if a.get("icon") == "orange":
        ix, iy = cx - tw / 2 + 52, cy + 2
        d.append(f'<circle cx="{ix}" cy="{iy}" r="24" fill="#150a02"/><circle cx="{ix}" cy="{iy}" r="20" fill="#ff9a2a"/>')
        for i in range(8):
            t = math.radians(i * 45)
            d.append(f'<line x1="{ix}" y1="{iy}" x2="{ix+20*math.sin(t):.1f}" y2="{iy-20*math.cos(t):.1f}" stroke="#150a02" stroke-width="2"/>')
        d.append(f'<circle cx="{ix}" cy="{iy}" r="3" fill="#150a02"/>')
    d.append(f'<text x="{tx+1.5}" y="{cy+12}" text-anchor="middle" font-family="{fam}" font-size="{fs}" {style} letter-spacing="{ls}" fill="#000" opacity=".5">{label}</text>')
    d.append(f'<text x="{tx}" y="{cy+10}" text-anchor="middle" font-family="{fam}" font-size="{fs}" {style} letter-spacing="{ls}" fill="{a["pt"]}">{label}</text>')
    f2 = f.get("font2") or SANS
    d.append(f'<text x="{tx}" y="{cy+34}" text-anchor="middle" font-family="{f2}" font-size="12" font-weight="600" letter-spacing="1.2" fill="{a["pt"]}" opacity=".8">{a["tag"].upper()}</text>')
    if a.get("platestripe"):
        d.append(f'<rect x="{cx-tw/2+5:.0f}" y="{cy+38}" width="{tw-10:.0f}" height="4" fill="{a["platestripe"]}"/>')
    # desgaste sutil: roces de borde (semilla por amp, estable entre corridas)
    for _ in range(6):
        _ey = rnd.uniform(12, 46) if rnd.random() < .5 else rnd.uniform(494, 528)
        _ex = rnd.uniform(60, 1080)
        _ln = rnd.uniform(16, 64)
        _dy = rnd.uniform(-6, 6)
        _c = "#fff" if rnd.random() < .6 else "#000"
        d.append(f'<line x1="{_ex:.0f}" y1="{_ey:.0f}" x2="{_ex + _ln:.0f}" y2="{_ey + _dy:.0f}" stroke="{_c}" stroke-opacity="{rnd.uniform(.03, .07):.2f}" stroke-width="{rnd.uniform(.7, 1.5):.1f}" stroke-linecap="round"/>')
    # cierre: un hilo de micro-grano (para que el plano grande no se vea plano)
    # y una vineta suave. Se retiraron el reflejo especular diagonal y el velo
    # de polvo: dos capas mas de neblina sobre todo el frontal.
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="#808080" filter="url(#grain)" opacity=".03"/>')
    d.append('<radialGradient id="edge" cx=".5" cy=".45" r=".78">'
             '<stop offset=".62" stop-color="#000" stop-opacity="0"/>'
             '<stop offset="1" stop-color="#0a0704" stop-opacity=".18"/></radialGradient>')
    d.append('<rect x="0" y="0" width="1200" height="540" rx="18" fill="url(#edge)"/>')
    d.append("</svg>")
    return "".join(d)


import json, shutil
shutil.rmtree("out", ignore_errors=True)
for dd in ["preview", "fondos", "perillas", "filmstrips"]:
    os.makedirs(f"out/{dd}", exist_ok=True)
S, FR = 112, 101

def knob_defs(a):
    return (f'<radialGradient id="cap" cx=".35" cy=".3" r=".9"><stop offset="0" stop-color="{a["cap"][0]}"/><stop offset="1" stop-color="{a["cap"][1]}"/></radialGradient>'
      f'<linearGradient id="ring" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{a["ring"][0]}"/><stop offset=".5" stop-color="{a["ring"][1]}"/><stop offset="1" stop-color="{a["ring"][0]}"/></linearGradient>')

def knob_svg(a, ang):
    _, _, kr = ctrl_geo(a)   # mismo radio que el hueco del fondo
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{S}" height="{S}" viewBox="0 0 {S} {S}"><defs>'
      f'{knob_defs(a)}</defs>{knob(56, 56, kr, ang, a, "body")}</svg>')

for a in AMPS:
    full = build(a)
    bare = build(a, knobs=False)
    i_ = a["id"]
    open(f"out/preview/{i_}.svg", "w", encoding="utf-8").write(full)
    open(f"out/fondos/{i_}_fondo.svg", "w", encoding="utf-8").write(bare)
    k0 = knob_svg(a, 0)
    open(f"out/perillas/{i_}_perilla.svg", "w", encoding="utf-8").write(k0)
    # filmstrip: 101 fotogramas apilados en un solo SVG (render por ancho)
    gs = []
    _, _, kr = ctrl_geo(a)   # mismo radio que el hueco del fondo
    for f in range(FR):
        ang = -135 + 270 * f / (FR - 1)
        gs.append(f'<g transform="translate(0,{f * S})">{knob(56, 56, kr, ang, a, "body")}</g>')
    strip = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{S}" height="{S * FR}" viewBox="0 0 {S} {S * FR}">'
             f'<defs>{knob_defs(a)}</defs>{"".join(gs)}</svg>')
    open(f"out/filmstrips/{i_}_filmstrip.svg", "w", encoding="utf-8").write(strip)
    json.dump(dict(amp=a["name"], fondo_size=[1200, 540], perilla_size=[S, S], filmstrip_frames=FR,
        angulo_min=-135, angulo_max=135, perillas=a["_pos"]), open(f"out/fondos/{i_}_posiciones.json", "w"), indent=1, ensure_ascii=False)

print("SVGs listos en out/ -> renderizar PNG con _svgrender/render.js")
