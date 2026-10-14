# -*- coding: utf-8 -*-
"""
Genera amp_presets_data.h: el banco de fabrica de presets.

Un "lote" por amplificador: 6 presets por amp (72 en total), agrupados por
familia sonora para que las recetas sean coherentes (un Fender limpio no
pide los mismos empujes que un Recto).

Los valores base de cada amplificador son los 12 presets ya afinados a mano en
la version anterior (un preset por amp): aqui solo se derivan seis variantes
deliberadas por amp (limpio / edge / crunch / solo / ambiente / especial) con
deltas de perillas y una cadena de pedales explicita.

    python gen_presets.py     -> escribe amp_presets_data.h (no editar a mano)

Reglas que se respetan (las mismas del motor):
 - Cadena: como mucho 6 pedales por fila y sin duplicados entre las dos filas.
 - fxOn[f] = true exactamente para los pedales que van montados en la cadena
   (el motor solo procesa los que estan en la cadena; los demas quedarian
   encendidos sin sonar y encendiendo luces en la paleta).
 - Perillas de amp en 0..100.
"""

FX_GATE, FX_COMP, FX_DRIVE, FX_MOD, FX_DELAY, FX_REVERB = 0, 1, 2, 3, 4, 5
FX_TREM, FX_PHASER, FX_FLANGER, FX_EQ = 6, 7, 8, 9
FX_OD, FX_FUZZ, FX_DIST, FX_CHORUS, FX_TAPE, FX_SPRING, FX_BOOST, FX_WAH = 10, 11, 12, 13, 14, 15, 16, 17

# Valores de arranque de cada pedal (los mismos que usaba el banco anterior):
# solo importan para los pedales montados; el resto son los de fabrica.
FX_BASE = {
    FX_GATE: (-58, 22), FX_COMP: (30, 45), FX_DRIVE: (0, 50), FX_MOD: (0, 50),
    FX_DELAY: (0, 40), FX_REVERB: (38, 30), FX_TREM: (42, 38), FX_PHASER: (0, 40),
    FX_FLANGER: (0, 40), FX_EQ: (50, 52), FX_OD: (0, 50), FX_FUZZ: (0, 50),
    FX_DIST: (0, 45), FX_CHORUS: (34, 40), FX_TAPE: (0, 35), FX_SPRING: (48, 44),
    FX_BOOST: (55, 50), FX_WAH: (0, 50),
}

# Modelos (indice = modelo de amp, el mismo que kAmpNames)
AMP_NAMES = [
    "SPARKLE TWIN", "CHIME 30", "AMBER TWEED", "CHORUS 120", "MARAUDER 800",
    "LEAD 1959", "TANGERINE 120", "LONDON BLUES", "SESSION HOT", "DUO RECTO",
    "KRAFT V4", "HAVOC 100", "PERFIL",
]

# Valores base por amp: los 12 presets afinados de la version anterior.
# amp = GAIN BASS MIDDLE TREBLE VOLUME PRESENCE
BASE = [
    # amp, master, cab, mic, dist
    ((28, 58, 52, 62, 74, 50), 72.0, 1, 2, 1),
    ((45, 50, 58, 64, 70, 58), 70.0, 0, 0, 0),
    ((55, 60, 55, 45, 66, 40), 68.0, 0, 0, 0),
    ((25, 55, 50, 58, 78, 45), 74.0, 1, 2, 2),
    ((62, 58, 65, 55, 66, 62), 68.0, 2, 0, 1),
    ((70, 60, 62, 58, 64, 55), 66.0, 2, 1, 0),
    ((58, 65, 70, 45, 64, 42), 66.0, 2, 0, 0),
    ((48, 56, 62, 52, 68, 50), 70.0, 1, 1, 1),
    ((55, 54, 60, 50, 66, 52), 68.0, 5, 1, 1),
    ((72, 68, 38, 58, 62, 55), 64.0, 3, 0, 1),
    ((68, 60, 55, 62, 64, 60), 66.0, 3, 0, 1),
    ((78, 62, 48, 60, 62, 58), 66.0, 3, 2, 0),
    ((50, 50, 50, 50, 60, 50), 70.0, 0, 0, 0),   # PERFIL: base neutra, manda la captura
]

# Familia sonora: A limpios con headroom, B crunch, C alta ganancia
FAMILY = ["A", "A", "B", "A", "B", "B", "B", "A", "A", "C", "C", "C", "A"]

# Los seis nombres de cada lote (curados por amplificador, no plantilla).
NAMES = [
    ["LIMPIO DIAMANTE", "TWANG DE PUENTE", "ROMPE SUAVE", "SOLO CREMA", "NIEBLA PLATEADA", "FUZZ DE GARAJE"],
    ["CAMPANA PURA", "JANGLE BRIT", "EDGE VOX", "SOLO CRISTAL", "TREMOLO AZUL", "FUZZ BEATLE"],
    ["TWEED LIMPIO", "EDGE DE NOCHE", "CRUNCH 5E3", "SOLO CALIENTE", "SALA DE MADERA", "TREMOLO DE TWEED"],
    ["PRISTINO", "CORO ANCHO", "ROTURA SOLIDA", "SOLO VIDRIO", "CIELO AZUL", "AUTO-WAH FUNK"],
    ["PLEXI LIMPIO", "AC/DC", "CRUNCH 800", "SOLO 800", "ECO DE SALA", "FUZZ MARSHALL"],
    ["PLEXI AIRE", "ROMPE PLEXI", "CRUNCH 1959", "LEAD CLASICO", "SALA PLEXI", "VIENTO PSICODELICO"],
    ["NARANJA LIMPIA", "ROMPE NARANJA", "CRUNCH GORDO", "SOLO GORDO", "NIEBLA NARANJA", "FUZZ NARANJA"],
    ["BLUES LIMPIO", "BLUES MORDIDO", "CRUNCH BLUES", "SOLO BLUES", "HABITACION BLUES", "WAH BLUES"],
    ["SESION LIMPIA", "TERCIOPELO", "CRUNCH VOCAL", "SOLO VOCAL", "PAD VOCAL", "FUZZ CANTADO"],
    ["RECTO LIMPIO", "RECTO CRUNCH", "RECTO MODERNO", "SOLO RECTO", "RECTO ESPACIO", "MURO DE RECTO"],
    ["KRAFT LIMPIO", "KRAFT BORDE", "KRAFT APRETADO", "SOLO KRAFT", "KRAFT ETEREO", "MURO KRAFT"],
    ["HAVOC LIMPIO", "HAVOC FILO", "HAVOC SATURADO", "SOLO HAVOC", "HAVOC NEBLINA", "HAVOC CAOS"],
    ["CAPTURA LIMPIA", "PERFIL BORDE", "PERFIL CRUNCH", "SOLO NEURONAL", "SALA NEURONAL", "PERFIL ESPECIAL"],
]

# Recetas por familia. Cada slot = (dg, db, dm, dt, dv, dp, dmaster, mic, dist,
# pre, post) con pre/post = [(tipo, p0, p1), ...].  -1 en mic/dist = usar el del
# amplificador.
RECIPES = {
    "A": [
        # 0 LIMPIO: todo el headroom, compresor de estudio y un muelle corto
        (-16, +6, -6, -4, +4, -4, +1.0, 2, 1,
         [(FX_GATE, -58, 20), (FX_COMP, 45, 55)], [(FX_SPRING, 40, 26)]),
        # 1 EDGE: apenas rompe, brillo y un empujon limpio
        (-4, +2, -2, +6, +2, +2, +1.0, 2, 1,
         [(FX_GATE, -56, 24), (FX_COMP, 35, 50), (FX_BOOST, 45, 60)], [(FX_SPRING, 46, 30)]),
        # 2 CRUNCH: overdrive delante del amp, medios al frente
        (+10, 0, +6, 0, 0, +4, 0.0, 1, 1,
         [(FX_GATE, -54, 26), (FX_COMP, 28, 45), (FX_OD, 52, 52)], [(FX_SPRING, 48, 32)]),
        # 3 SOLO: dos etapas de empuje y un delay corto
        (+20, 0, +8, +4, -2, +6, -2.0, 1, 2,
         [(FX_GATE, -50, 30), (FX_OD, 60, 54), (FX_BOOST, 52, 62)], [(FX_DELAY, 34, 38), (FX_SPRING, 44, 28)]),
        # 4 AMBIENTE: coro, delay largo y reverb; micro lejos
        (-8, 0, -2, +4, +2, -2, -1.0, 2, 2,
         [(FX_GATE, -58, 22), (FX_COMP, 38, 50), (FX_CHORUS, 30, 42)], [(FX_DELAY, 46, 42), (FX_REVERB, 52, 38)]),
    ],
    "B": [
        (-18, +4, -4, -2, +6, -4, +1.0, 1, 1,
         [(FX_GATE, -58, 20), (FX_COMP, 30, 45)], [(FX_SPRING, 40, 26)]),
        (-6, +2, +4, +2, +4, +2, +1.0, 1, 1,
         [(FX_GATE, -56, 22), (FX_DRIVE, 38, 50)], [(FX_SPRING, 44, 28)]),
        (+6, 0, +6, 0, 0, +4, 0.0, 0, 1,
         [(FX_GATE, -54, 24), (FX_DRIVE, 52, 52), (FX_BOOST, 48, 60)], [(FX_SPRING, 46, 30)]),
        (+16, 0, +8, +2, -4, +8, -2.0, 0, 1,
         [(FX_GATE, -50, 28), (FX_OD, 58, 54), (FX_BOOST, 56, 64)], [(FX_DELAY, 32, 40), (FX_SPRING, 42, 28)]),
        (-10, 0, +2, +4, +4, -2, -1.0, 2, 2,
         [(FX_GATE, -58, 22), (FX_COMP, 36, 48), (FX_PHASER, 26, 40)], [(FX_TAPE, 42, 46), (FX_REVERB, 46, 36)]),
    ],
    "C": [
        (-26, +4, +8, -2, +8, -6, +2.0, 0, 2,
         [(FX_GATE, -56, 18), (FX_COMP, 28, 42)], [(FX_REVERB, 34, 22)]),
        (-14, 0, +6, +2, +4, -2, +1.0, 0, 1,
         [(FX_GATE, -54, 20), (FX_OD, 42, 48)], [(FX_REVERB, 32, 20)]),
        (-2, 0, +6, +4, 0, +2, 0.0, 0, 0,
         [(FX_GATE, -52, 22), (FX_DRIVE, 50, 50)], [(FX_DELAY, 28, 34)]),
        (+8, 0, +8, +4, -6, +6, -2.0, 0, 1,
         [(FX_GATE, -50, 26), (FX_OD, 56, 52), (FX_BOOST, 54, 62)], [(FX_DELAY, 34, 42)]),
        (-12, +2, 0, +4, +6, -4, -1.0, 1, 2,
         [(FX_GATE, -58, 22), (FX_COMP, 34, 46), (FX_CHORUS, 34, 40)], [(FX_TAPE, 46, 48), (FX_REVERB, 50, 34)]),
    ],
}

# Slot 5 (ESPECIAL): el caracter propio de cada amplificador. (deltas, pre, post)
SPECIAL = [
    ((+6, 0, 0, +2, 0, 0), [(FX_GATE, -52, 26), (FX_FUZZ, 40, 52)], [(FX_DELAY, 30, 36)]),          # SPARKLE TWIN
    ((+4, 0, +2, +2, 0, 0), [(FX_GATE, -52, 26), (FX_FUZZ, 38, 56)], [(FX_SPRING, 46, 30)]),        # CHIME 30
    ((0, 0, +2, 0, +2, -2), [(FX_GATE, -56, 22), (FX_TREM, 46, 52)], [(FX_SPRING, 42, 28)]),        # AMBER TWEED
    ((-2, 0, 0, +2, -2, 0), [(FX_COMP, 40, 52), (FX_WAH, 48, 55)], [(FX_DELAY, 30, 32)]),            # CHORUS 120
    ((+4, 0, +4, 0, 0, +2), [(FX_GATE, -50, 26), (FX_FUZZ, 44, 52)], [(FX_SPRING, 44, 28)]),        # MARAUDER 800
    ((+6, 0, +4, 0, -2, +2), [(FX_GATE, -52, 28), (FX_PHASER, 34, 50)], [(FX_TAPE, 48, 52)]),        # LEAD 1959
    ((+6, 0, +4, -2, 0, +2), [(FX_GATE, -52, 28), (FX_FUZZ, 46, 50)], [(FX_DELAY, 28, 34)]),         # TANGERINE 120
    ((0, +2, +4, 0, +2, 0), [(FX_GATE, -56, 22), (FX_COMP, 34, 46), (FX_WAH, 44, 52)], [(FX_SPRING, 42, 26)]),   # LONDON BLUES
    ((+2, 0, +2, +2, -2, 0), [(FX_GATE, -52, 26), (FX_FUZZ, 36, 48)], [(FX_DELAY, 30, 36)]),        # SESSION HOT
    ((-4, 0, +6, -2, +2, -2), [(FX_GATE, -48, 30), (FX_DIST, 64, 42), (FX_BOOST, 56, 60)], [(FX_DELAY, 28, 36)]),  # DUO RECTO
    ((-6, 0, +6, -4, +4, -4), [(FX_GATE, -48, 30), (FX_DIST, 60, 44), (FX_BOOST, 52, 58)], [(FX_DELAY, 26, 34)]),  # KRAFT V4
    ((-2, +2, +4, -2, +4, -4), [(FX_GATE, -46, 32), (FX_FUZZ, 34, 56), (FX_DIST, 58, 40)], [(FX_REVERB, 40, 26)]), # HAVOC 100
    ((+2, 0, 0, 0, +2, 0), [(FX_GATE, -54, 24), (FX_COMP, 36, 48)], [(FX_DELAY, 30, 34)]),          # PERFIL
]

MAX_ROW = 6


def clamp(v, lo=0, hi=100):
    return max(lo, min(hi, v))


def knobs(base, d):
    return tuple(clamp(base[i] + d[i]) for i in range(6))


def chain(pedals):
    """Valida la cadena y devuelve (tipos, fx) para el preset."""
    assert len(pedals) <= MAX_ROW, pedals
    types = [p[0] for p in pedals]
    assert len(set(types)) == len(types), types
    return types


def fx_table(pre, post):
    fx = dict(FX_BASE)
    for t, p0, p1 in list(pre) + list(post):
        fx[t] = (p0, p1)
    on = [1 if t in [p[0] for p in pre] + [p[0] for p in post] else 0 for t in range(18)]
    return fx, on


def rows():
    out = []
    for amp in range(13):
        base, master, cab, bmic, bdist = BASE[amp]
        fam = RECIPES[FAMILY[amp]]
        for slot in range(6):
            if slot == 5:
                dd, pre, post = SPECIAL[amp]
                dg, db, dm, dt, dv, dp = dd
                dmaster, mic, dist = 0.0, bmic, bdist
            else:
                dg, db, dm, dt, dv, dp, dmaster, mic, dist, pre, post = fam[slot]
            pre_t, post_t = chain(pre), chain(post)
            fx, on = fx_table(pre, post)
            amp_k = knobs(base, (dg, db, dm, dt, dv, dp))
            out.append({
                "name": NAMES[amp][slot],
                "model": amp,
                "amp": amp_k,
                "fx": fx,
                "on": on,
                "master": clamp(master + dmaster, 40.0, 90.0),
                "cab": cab,
                "mic": mic if mic >= 0 else bmic,
                "dist": dist if dist >= 0 else bdist,
                "pre": pre_t,
                "post": post_t,
            })
    return out


def emit(path):
    lines = []
    w = lines.append
    w("// amp_presets_data.h - GENERADO por gen_presets.py (no editar a mano)")
    w("// Banco de fabrica: 6 lotes por amplificador, en orden (amp 0 slot 0..5,")
    w("// amp 1 slot 0..5, ...). Cada entrada: nombre, modelo, perillas, params de")
    w("// FX, pedales encendidos, master, cabina, y la cadena (pre / post).")
    r = rows()
    for i, e in enumerate(r):
        slot = i % 6
        fx = "".join("{ %g, %g }, " % e["fx"][i] for i in range(18))
        on = "".join(("%d%s" % (e["on"][i], "," if i < 17 else "")) for i in range(18))
        pre = ", ".join(str(t) for t in e["pre"] + [0] * (6 - len(e["pre"])))
        post = ", ".join(str(t) for t in e["post"] + [0] * (6 - len(e["post"])))
        w('    { L"%s", %d, { %s },' % (e["name"], e["model"], ", ".join("%g" % v for v in e["amp"])))
        w("      { %s " % fx)
        w("      },")
        w("      { %s }," % on)
        w("      %.1ff, %d, %d, %d, %d, { %s }, %d, { %s } },   // %s #%d" %
          (e["master"], e["cab"], e["mic"], e["dist"], len(e["pre"]), pre, len(e["post"]), post,
           AMP_NAMES[e["model"]], e["model"] * 6 + (slot + 1)))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return len(r)


if __name__ == "__main__":
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    n = emit(os.path.join(here, "amp_presets_data.h"))
    # comprobaciones de integridad del banco
    data = rows()
    assert n == 78, n
    for amp in range(13):
        lot = [e for e in data if e["model"] == amp]
        assert len(lot) == 6, (amp, len(lot))
        assert all(len(e["pre"]) <= MAX_ROW and len(e["post"]) <= MAX_ROW for e in lot)
        assert all(len(set(e["pre"]) & set(e["post"])) == 0 for e in lot)
    print("GEN_PRESETS_OK", n, "presets")
