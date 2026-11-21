#!/usr/bin/env python3
"""Genera el anexo "Tiempo de desarrollo del software" del informe final.

    python tools/informe-tiempo.py            # HTML + PDF + Word
    python tools/informe-tiempo.py --sin-pdf  # sin Chrome: solo HTML + Word

Escribe en BioSoft/anexo-tiempo/ (carpeta de tesis, fuera de git):
    anexo-tiempo-desarrollo.html / .pdf / .docx, grafico.png y datos.json

Que sale de donde:

* **Horas con rastro** (medidas). Se juntan los rastros con fecha y hora que
  el trabajo dejo en esta PC: historial local de VS Code (guarda copias de
  cada archivo, incluso de carpetas ya borradas como C:\\Repositorio),
  compilaciones de Arduino IDE, sketches, recientes de Windows, SquareLine,
  archivos y reflog del repo, compilaciones de PlatformIO y registros de
  Claude Code. Dos rastros a 2 h o menos forman una sesion; cada sesion suma
  su duracion + 45 min de arranque. Es un piso: lo que no toca la PC no queda.
* **Horas estimadas**: semanas de calendario de cada periodo x el ritmo
  semanal que declaro el autor para esa etapa (tools/informe-tiempo.json).
* **Codigo**: el migrado se cuenta del primer commit; el de cada generacion
  de prototipos, de la ultima copia de cada archivo en el historial de VS Code.

Todo lo que no se puede deducir (fechas de corte, ritmo, que se hizo en cada
periodo, que carpetas no son del proyecto) vive en tools/informe-tiempo.json.
Para actualizar el anexo: editar ese archivo (fecha 'fin', periodos) y correr
esto de nuevo. Solo usa la biblioteca estandar; el Word se escribe en OOXML a
mano, igual que tools/manual-a-word.py (estilos Heading1/Heading2 por
w:styleId, si no Word no arma el panel de navegacion).
"""

import argparse
import datetime as dt
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from urllib.parse import unquote
from xml.sax.saxutils import escape

RAIZ = Path(__file__).resolve().parent.parent
CONFIG = Path(__file__).with_suffix(".json")
SALIDA = RAIZ / "BioSoft" / "anexo-tiempo"
HOME = Path.home()
ART = dt.timezone(dt.timedelta(hours=-3))

CORTE_SESION_H, ARRANQUE_MIN = 2, 45
SENSIBILIDAD = [(1, 30), (3, 60)]

COLOR = {"propio": "#2a78d6", "claude": "#eb6834"}
TINTA, MUTED, LINEA, ACENTO = "#14181d", "#535c66", "#d9dee3", "#17527f"


# ------------------------------------------------------------ utilidades ----
def fecha(s):
    return dt.date.fromisoformat(s)


def ms_de(d, h=0, m=0):
    return dt.datetime(d.year, d.month, d.day, h, m, tzinfo=ART).timestamp() * 1000


def dia_art(ms):
    return dt.datetime.fromtimestamp(ms / 1000, ART).date()


def miles(n):
    return f"{int(round(n)):,}".replace(",", ".")


def dec(x, n=1):
    return f"{x:.{n}f}".replace(".", ",")


MESES = ["ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"]


def mes_txt(clave):
    a, m = clave.split("-")
    return f"{MESES[int(m) - 1]} {a[2:]}"


def mtime_ms(p):
    try:
        return os.stat(p).st_mtime * 1000
    except OSError:
        return None


# ------------------------------------------------------------- rastros ----
def rastros(cfg):
    """Devuelve (eventos [(ms, fuente)], copias {ruta: archivo de la ultima copia})."""
    ev, copias = [], {}
    excluir = re.compile("|".join(re.escape(x) for x in cfg["excluir"]), re.I)

    hist = HOME / "AppData/Roaming/Code/User/History"
    if hist.is_dir():
        for d in hist.iterdir():
            f = d / "entries.json"
            if not f.is_file():
                continue
            try:
                j = json.loads(f.read_text(encoding="utf8"))
            except (ValueError, OSError):
                continue
            ruta = unquote(j.get("resource", "")).replace("file:///", "")
            if excluir.search(ruta):
                continue
            for e in j.get("entries", []):
                ev.append((e["timestamp"], "VS Code"))
                c = copias.get(ruta)
                if c is None or e["timestamp"] > c[0]:
                    copias[ruta] = (e["timestamp"], d / e["id"])

    builds = HOME / "AppData/Local/arduino/sketches"
    if builds.is_dir():
        for d in builds.iterdir():
            if d.is_dir():
                for f in d.iterdir():
                    if f.is_file():
                        ev.append((mtime_ms(f), "Arduino IDE"))

    sk = HOME / "OneDrive/Documentos/Arduino"
    if sk.is_dir():
        for p, dirs, files in os.walk(sk):
            dirs[:] = [x for x in dirs if x != "libraries"]
            for n in files:
                if n.lower().endswith((".ino", ".hpp", ".h", ".cpp")):
                    ev.append((mtime_ms(os.path.join(p, n)), "Otros"))

    rec = HOME / "AppData/Roaming/Microsoft/Windows/Recent"
    if rec.is_dir():
        for f in rec.iterdir():
            if f.is_file() and any(f.name.lower().startswith(x.lower()) for x in cfg["recientes"]):
                ev.append((mtime_ms(f), "Otros"))

    sq = HOME / "SquareLine"
    if sq.is_dir():
        for p, _, files in os.walk(sq):
            for n in files:
                ev.append((mtime_ms(os.path.join(p, n)), "SquareLine"))

    # BioSoft/ (tesis, inventarios) si cuenta: son trabajo. Lo que no cuenta es la salida de este script.
    podar = {"node_modules", ".pio", ".git", "dist"}
    for p, dirs, files in os.walk(RAIZ):
        dirs[:] = [x for x in dirs if x not in podar and Path(p, x) != SALIDA]
        for n in files:
            ev.append((mtime_ms(os.path.join(p, n)), "Repositorio"))

    for placa in ("esp32", "mega2560"):
        pio = RAIZ / placa / ".pio"
        if pio.is_dir():
            for p, _, files in os.walk(pio):
                if p.count(os.sep) - str(pio).count(os.sep) > 2:
                    continue
                for n in files:
                    if n.startswith("firmware") or n.endswith(".elf"):
                        ev.append((mtime_ms(os.path.join(p, n)), "Otros"))

    reflog = RAIZ / ".git/logs/HEAD"
    if reflog.is_file():
        for l in reflog.read_text(encoding="utf8", errors="replace").splitlines():
            m = re.search(r"> (\d{10}) [+-]\d{4}", l)
            if m:
                ev.append((int(m.group(1)) * 1000, "git"))

    proy = HOME / ".claude/projects" / re.sub(r"[:\\/]", "-", str(RAIZ)).replace(" ", "-")
    if not proy.is_dir():
        proy = HOME / ".claude/projects/c--MigracionProyectoFinal-Repo-BioSoft"
    if proy.is_dir():
        for f in proy.glob("*.jsonl"):
            for l in f.read_text(encoding="utf8", errors="replace").splitlines():
                m = re.search(r'"timestamp":"([^"]+)"', l)
                if m:
                    try:
                        t = dt.datetime.fromisoformat(m.group(1).replace("Z", "+00:00"))
                        ev.append((t.timestamp() * 1000, "Claude Code"))
                    except ValueError:
                        pass

    return [e for e in ev if e[0]], copias


def sesiones(ts, corte_h, arranque_min):
    out, ini, prev = [], None, None
    for t in sorted(ts):
        if ini is None or t - prev > corte_h * 3600e3:
            if ini is not None:
                out.append((ini, prev))
            ini = t
        prev = t
    if ini is not None:
        out.append((ini, prev))
    return [(a, (b - a) / 3600e3 + arranque_min / 60) for a, b in out]


# --------------------------------------------------------------- calculo ----
def git(*args):
    return subprocess.run(["git", *args], cwd=RAIZ, capture_output=True, text=True, encoding="utf8").stdout


def calcular(cfg):
    ini, fin = fecha(cfg["inicio"]), fecha(cfg["fin"])
    corte = dt.datetime.fromisoformat(cfg["corte"]).replace(tzinfo=ART)
    corte_ms = corte.timestamp() * 1000
    t0, t1 = ms_de(ini), ms_de(fin + dt.timedelta(days=1))
    ev, copias = rastros(cfg)
    ev = [e for e in ev if t0 <= e[0] < t1]
    etapas = {e["id"]: e for e in cfg["etapas"]}
    ids = [e["id"] for e in cfg["etapas"]]
    de_etapa = {ids[0]: [t for t, _ in ev if t < corte_ms], ids[1]: [t for t, _ in ev if t >= corte_ms]}

    ses = {k: sesiones(v, CORTE_SESION_H, ARRANQUE_MIN) for k, v in de_etapa.items()}
    sens = [{"corte": c, "arranque": a,
             "horas": {k: sum(h for _, h in sesiones(v, c, a)) for k, v in de_etapa.items()}}
            for c, a in SENSIBILIDAD]

    commits = [dt.datetime.fromisoformat(l.strip()).astimezone(ART).date()
               for l in git("log", "--format=%aI").splitlines() if l.strip()]

    periodos = []
    for p in cfg["periodos"]:
        d, h = fecha(p["desde"]), fecha(p["hasta"])
        s = [x for x in ses[p["etapa"]] if d <= dia_art(x[0]) <= h]
        sem = ((h - d).days + 1) / 7
        periodos.append({**p, "semanas": sem, "estimadas": sem * etapas[p["etapa"]]["ritmo"],
                         "medidas": sum(x[1] for x in s), "dias": len({dia_art(x[0]) for x in s}),
                         "commits": sum(1 for c in commits if d <= c <= h)})

    resumen = {}
    for k in ids:
        ps = [p for p in periodos if p["etapa"] == k]
        dias = {dia_art(x[0]) for x in ses[k]}
        resumen[k] = {"semanas": sum(p["semanas"] for p in ps), "estimadas": sum(p["estimadas"] for p in ps),
                      "medidas": sum(x[1] for x in ses[k]), "dias": len(dias),
                      "habiles": sum(1 for x in dias if x.weekday() < 5),
                      "desde": min(p["desde"] for p in ps), "hasta": max(p["hasta"] for p in ps)}

    nsem = -(-((fin - ini).days + 1) // 7)
    con = [0] * nsem
    for t, _ in ev:
        con[int((t - t0) // (7 * 864e5))] += 1
    semanas_rastro = sum(1 for c in con if c)

    meses = {}
    for k in ids:
        for a, h in ses[k]:
            clave = (dia_art(a).strftime("%Y-%m"), k)
            m = meses.setdefault(clave, {"dias": set(), "sesiones": 0, "horas": 0.0})
            m["dias"].add(dia_art(a))
            m["sesiones"] += 1
            m["horas"] += h
    meses = [{"mes": c[0], "etapa": c[1], "dias": len(v["dias"]), "sesiones": v["sesiones"], "horas": v["horas"]}
             for c, v in sorted(meses.items(), key=lambda kv: (kv[0][0], ids.index(kv[0][1])))]

    fuentes = {}
    for _, f in ev:
        fuentes[f] = fuentes.get(f, 0) + 1

    # codigo por generacion: ultima copia de cada archivo en el historial de VS Code
    no_codigo = re.compile(r"lv_conf|User_Setup|/ui_|node_modules|\.test\.js|Arduino/libraries", re.I)
    gens = [{**g, "archivos": 0, "lineas": 0} for g in cfg["generaciones"]]
    for ruta, (_, archivo) in copias.items():
        if not re.search(r"\.(js|ts|hpp|h|cpp|c|ino)$", ruta, re.I) or no_codigo.search(ruta):
            continue
        g = next((g for g in gens if any(p.lower() in ruta.lower() for p in g["patrones"])), None)
        if g is None:
            continue
        try:
            g["lineas"] += len(archivo.read_text(encoding="utf8", errors="replace").split("\n"))
            g["archivos"] += 1
        except OSError:
            pass

    # codigo migrado: primer commit
    raiz_commit = git("rev-list", "--max-parents=0", "HEAD").split()[0]
    nombres = git("ls-tree", "-r", "--name-only", raiz_commit).splitlines()
    fuente = [n for n in nombres if n.endswith((".hpp", ".ino")) and n.split("/")[0] in ("esp32", "mega2560")]
    lineas = {n: git("show", f"{raiz_commit}:{n}").count("\n") for n in fuente}
    por_base = {}
    for n, l in lineas.items():
        por_base.setdefault(os.path.basename(n), []).append(l)
    duplicadas = sum(min(v) for v in por_base.values() if len(v) > 1)
    migrado = {"fecha": git("show", "-s", "--format=%as", raiz_commit).strip(),
               "archivos": len(fuente), "lineas": sum(lineas.values()),
               "sin_duplicados": sum(lineas.values()) - duplicadas,
               "interfaz": sum(1 for n in nombres if re.search(r"/ui[_.]", n))}

    return {"periodos": periodos, "resumen": resumen, "sensibilidad": sens, "meses": meses,
            "fuentes": fuentes, "rastros": len(ev), "semanas": nsem, "semanas_rastro": semanas_rastro,
            "generaciones": gens, "migrado": migrado, "corte": corte}


# ------------------------------------------------- documento (modelo) ----
def documento(cfg, r):
    """Arma el anexo como una lista de bloques que despues se dibujan en HTML y en Word."""
    et = cfg["etapas"]
    a, b = et[0]["id"], et[1]["id"]
    ra, rb = r["resumen"][a], r["resumen"][b]
    est_tot = ra["estimadas"] + rb["estimadas"]
    med_tot = ra["medidas"] + rb["medidas"]
    sem_tot = ra["semanas"] + rb["semanas"]
    d10 = lambda x: miles(round(x / 10) * 10)
    fin = fecha(cfg["fin"])
    corte = r["corte"]
    B = []

    B.append(("titulo", cfg["titulo"]))
    B.append(("meta", f"{cfg['proyecto']} · Autor: {cfg['autor']} · Datos al {fin.strftime('%d/%m/%Y')}"))

    B.append(("h1", "Resumen"))
    B.append(("p", f"El software de BioSoft llevó unas **{d10(est_tot)} horas de trabajo a lo largo de "
                   f"{round(sem_tot)} semanas**, entre el {fecha(cfg['inicio']).strftime('%d/%m/%Y')} y el "
                   f"{fin.strftime('%d/%m/%Y')}. El {round(100 * ra['estimadas'] / est_tot)} % de esas horas "
                   f"corresponde a la etapa en que el autor escribió el código, con ChatGPT en su versión gratuita "
                   f"como apoyo y revisión. Desde el {corte.strftime('%d/%m/%Y')}, Claude Code escribió código "
                   f"bajo la dirección del autor."))
    filas = []
    for e in et:
        x = r["resumen"][e["id"]]
        filas.append([e["nombre"], f"{fecha(x['desde']).strftime('%d/%m/%Y')} al {fecha(x['hasta']).strftime('%d/%m/%Y')}",
                      dec(x["semanas"]), f"{e['ritmo']} h", miles(x["medidas"]), "≈" + d10(x["estimadas"])])
    B.append(("tabla", {"cab": ["Etapa", "Período", "Semanas", "Ritmo semanal", "Horas con rastro", "Horas estimadas"],
                        "filas": filas, "pie": ["Total", "", dec(sem_tot), "", miles(med_tot), "≈" + d10(est_tot)],
                        "num": [2, 3, 4, 5], "anchos": [30, 24, 10, 12, 12, 12]}))
    B.append(("p", "Las **horas estimadas** multiplican las semanas de calendario por el ritmo promedio que estimó el autor "
                   f"para cada etapa: {et[0]['ritmo']} horas semanales en la primera y {et[1]['ritmo']} en la segunda. Las "
                   "**horas con rastro** son el piso comprobable: salen de registros con fecha y hora que quedaron en la "
                   "computadora."))

    for n, e in enumerate(et, 1):
        x = r["resumen"][e["id"]]
        B.append(("h1", f"Etapa {n}: {e['nombre']}"))
        B.append(("p", f"Va del {fecha(x['desde']).strftime('%d/%m/%Y')} al {fecha(x['hasta']).strftime('%d/%m/%Y')}, "
                       f"unas {round(x['semanas'])} semanas, con {x['dias']} días de actividad registrada "
                       f"({x['habiles']} hábiles y {x['dias'] - x['habiles']} de fin de semana). {e['texto']}"))
        ps = [p for p in r["periodos"] if p["etapa"] == e["id"]]
        filas = []
        for p in ps:
            t = p["trabajo"] + (f" {p['commits']} commits." if p["commits"] else "")
            filas.append([p["nombre"], t, dec(p["semanas"]), miles(p["medidas"]), miles(p["estimadas"])])
        B.append(("tabla", {"cab": ["Período", "Trabajo realizado", "Semanas", "Horas con rastro", "Horas estimadas"],
                            "filas": filas,
                            "pie": ["Total", "", dec(x["semanas"]), miles(x["medidas"]), miles(x["estimadas"])],
                            "num": [2, 3, 4], "anchos": [15, 51, 10, 12, 12]}))
        notas = [f"Horas estimadas: semanas × {e['ritmo']} h. Los totales se calculan sin redondear."]
        mayores = [p["nombre"].lower() for p in ps if p["medidas"] > p["estimadas"]]
        if mayores:
            lista = mayores[0] if len(mayores) == 1 else ", ".join(mayores[:-1]) + " y " + mayores[-1]
            notas.append(f"En {lista} las horas con rastro superan a las estimadas porque fueron períodos "
                         f"más intensos que el promedio: el ritmo de {e['ritmo']} horas semanales vale para la etapa completa.")
        if n == 1:
            notas.append("El último período es el de menos rastros porque se trabajó sobre todo en Arduino IDE, "
                         "que no guarda historial de ediciones.")
        B.append(("nota", " ".join(notas)))

        if n == 1:
            m, gens = r["migrado"], r["generaciones"]
            tot = sum(g["lineas"] for g in gens)
            B.append(("h2", "Código anterior a Claude Code"))
            B.append(("p", f"Al migrar, el {fecha(m['fecha']).strftime('%d/%m/%Y')}, ya había **{miles(m['lineas'])} líneas "
                           f"escritas a mano** en {m['archivos']} archivos del Mega y la ESP32, unas "
                           f"{miles(round(m['sin_duplicados'], -2))} sin contar los archivos duplicados entre placas. A eso se "
                           f"sumaban {m['interfaz']} archivos de interfaz generados con SquareLine. Esas líneas son el "
                           f"resultado final de un trabajo más largo: entre todas las generaciones de prototipos se "
                           f"escribieron unas **{miles(round(tot, -2))} líneas**."))
            B.append(("tabla", {"titulo": "Líneas por generación, según la última copia de cada archivo en VS Code",
                                "cab": ["Generación", "Carpetas", "Archivos", "Líneas"],
                                "filas": [[g["nombre"], g["carpetas"], str(g["archivos"]), miles(g["lineas"])] for g in gens],
                                "num": [2, 3], "anchos": [24, 50, 12, 14]}))
            B.append(("nota", "Las generaciones se solapan porque cada una reutiliza parte de la anterior, así que el total "
                              "no equivale a código único. La cifra excluye tests, interfaz generada y lo editado solo en "
                              "Arduino IDE, que no dejó copias."))

    B.append(("h1", "Detalle mensual"))
    B.append(("grafico", None))
    nombre = {e["id"]: e["corto"] for e in et}
    B.append(("tabla", {"cab": ["Mes", "Etapa", "Días con rastro", "Sesiones", "Horas con rastro"],
                        "filas": [[mes_txt(m["mes"]), nombre[m["etapa"]], str(m["dias"]), str(m["sesiones"]),
                                   dec(m["horas"])] for m in r["meses"]],
                        "num": [2, 3, 4], "anchos": [16, 30, 18, 16, 20]}))

    B.append(("h1", "Metodología"))
    B.append(("p", f"Se reunieron {miles(r['rastros'])} rastros con fecha y hora y se separaron en dos etapas con un corte "
                   f"el {corte.strftime('%d/%m/%Y')} a las {corte.strftime('%H:%M')}, primer registro de Claude Code en "
                   f"el proyecto."))
    que = {"VS Code": "Historial local: cada archivo guardado, incluso de carpetas ya borradas",
           "Arduino IDE": "Última compilación de cada sketch",
           "SquareLine": "Proyectos y copias de seguridad de la interfaz",
           "Claude Code": "Registros de sesiones que todavía se conservan",
           "Repositorio": "Fecha de última modificación de los archivos",
           "git": "Reflog: commits, merges y cambios de rama",
           "Otros": "Sketches en Documentos/Arduino, recientes de Windows y compilaciones de PlatformIO"}
    B.append(("tabla", {"cab": ["Fuente", "Qué registra", "Rastros"],
                        "filas": [[k, que.get(k, ""), miles(v)] for k, v in sorted(r["fuentes"].items(), key=lambda kv: -kv[1])],
                        "num": [2], "anchos": [20, 66, 14]}))
    s1, s2 = r["sensibilidad"]
    B.append(("lista", [
        f"**Horas con rastro.** Dos rastros separados por {CORTE_SESION_H} horas o menos forman una sesión. Cada sesión "
        f"suma su duración más {ARRANQUE_MIN} minutos de arranque, porque antes del primer guardado ya se estaba trabajando.",
        f"**Sensibilidad.** Con {s1['corte']} hora de corte y {s1['arranque']} minutos de arranque da "
        f"{miles(s1['horas'][a])} + {miles(s1['horas'][b])} h. Con {s2['corte']} horas y {s2['arranque']} minutos da "
        f"{miles(s2['horas'][a])} + {miles(s2['horas'][b])} h.",
        f"**Horas estimadas.** Semanas de calendario de cada período por el ritmo promedio que estimó el autor: "
        f"{et[0]['ritmo']} horas en la primera etapa y {et[1]['ritmo']} en la segunda.",
        f"**Semanas sin rastro.** Todas las semanas cuentan por igual, también las que no tienen registros y las de menor "
        f"actividad. De {r['semanas']} semanas, {r['semanas_rastro']} tienen algún rastro.",
        f"**Distancia entre ambas cifras.** Las {miles(med_tot)} horas con rastro son el {round(100 * med_tot / est_tot)} % "
        f"de las {d10(est_tot)} estimadas. La diferencia depende del ritmo declarado y no se puede comprobar con estos registros.",
    ]))
    B.append(("p", "Las fechas y las horas con rastro salen de los registros. El ritmo semanal de cada etapa es una estimación del autor."))

    B.append(("h1", "Alcance y limitaciones"))
    vals = {"p1_est": miles(r["periodos"][0]["estimadas"]), "e1_est": miles(ra["estimadas"])}
    B.append(("lista", [x.format_map(vals) for x in cfg["limitaciones"]]))
    return B


# ---------------------------------------------------------------- grafico ----
def grafico_svg(cfg, r):
    ids = [e["id"] for e in cfg["etapas"]]
    ini, fin = fecha(cfg["inicio"]), fecha(cfg["fin"])
    meses, d = [], dt.date(ini.year, ini.month, 1)
    while d <= fin:
        meses.append(d.strftime("%Y-%m"))
        d = dt.date(d.year + (d.month == 12), d.month % 12 + 1, 1)
    val = {(m["mes"], m["etapa"]): m["horas"] for m in r["meses"]}
    tot = [sum(val.get((m, k), 0) for k in ids) for m in meses]
    ymax = max(20, -(-max(tot) // 20) * 20)
    W, H, L, R, T, Bm, bw = 800, 280, 36, 8, 22, 30, 24
    y = lambda v: T + (H - T - Bm) * (1 - v / ymax)
    band = (W - L - R) / len(meses)
    s = []
    for v in range(0, int(ymax) + 1, 20):
        s.append(f'<line x1="{L}" x2="{W - R}" y1="{y(v):.1f}" y2="{y(v):.1f}" stroke="{LINEA}" stroke-width="1"/>'
                 f'<text x="{L - 6}" y="{y(v) + 4:.1f}" text-anchor="end" fill="{MUTED}">{v}</text>')
    etiquetar = set()
    for k in ids:
        serie = [(val.get((m, k), 0), i) for i, m in enumerate(meses)]
        if any(v for v, _ in serie):
            etiquetar.add(max(serie)[1])

    def tapa(x, yt, w, h, c):
        rr = min(4, h)
        return (f'<path fill="{c}" d="M{x:.1f} {yt + h:.1f}V{yt + rr:.1f}Q{x:.1f} {yt:.1f} {x + rr:.1f} {yt:.1f}'
                f'H{x + w - rr:.1f}Q{x + w:.1f} {yt:.1f} {x + w:.1f} {yt + rr:.1f}V{yt + h:.1f}Z"/>')

    base = y(0)
    for i, m in enumerate(meses):
        x = L + band * i + (band - bw) / 2
        partes = [(k, val.get((m, k), 0)) for k in ids if val.get((m, k), 0) > 0]
        acum = base
        for j, (k, v) in enumerate(partes):
            h = base - y(v)
            top = acum - h - (2 if j else 0)
            if j == len(partes) - 1:
                s.append(tapa(x, top, bw, h, COLOR[k]))
            else:
                s.append(f'<rect x="{x:.1f}" y="{top:.1f}" width="{bw}" height="{h:.1f}" fill="{COLOR[k]}"/>')
            acum = top
        if i % 3 == 0:
            s.append(f'<text x="{x + bw / 2:.1f}" y="{H - 10}" text-anchor="middle" fill="{MUTED}">{mes_txt(m)}</text>')
        if i in etiquetar:
            s.append(f'<text x="{x + bw / 2:.1f}" y="{y(tot[i]) - 6:.1f}" text-anchor="middle" fill="{TINTA}" '
                     f'font-weight="600">{round(tot[i])} h</text>')
    pico = max(range(len(meses)), key=lambda i: tot[i])
    aria = (f"Columnas de horas con rastro por mes, de {mes_txt(meses[0])} a {mes_txt(meses[-1])}. "
            f"El máximo es {mes_txt(meses[pico])} con {round(tot[pico])} horas. Los datos completos están en la tabla siguiente.")
    return (f'<svg viewBox="0 0 {W} {H}" role="img" aria-label="{html.escape(aria)}" '
            f'font-family="\'Segoe UI\', system-ui, sans-serif" font-size="12">{"".join(s)}</svg>')


def leyenda_html(cfg):
    return "".join(f'<span><span class="dot" style="background:{COLOR[e["id"]]}"></span>{html.escape(e["corto"])}</span>'
                   for e in cfg["etapas"])


# ------------------------------------------------------------------ HTML ----
def negritas_html(t):
    return re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", html.escape(t, quote=False))


CSS = f"""
/* Anexo de informe: una columna, tablas a todo el ancho, A4 al imprimir. Un solo tema, claro, como el papel. */
:root{{color-scheme:light;--bg:#ffffff;--ink:{TINTA};--muted:{MUTED};--line:{LINEA};--accent:{ACENTO};--fill:#f2f5f8;
  --display:"Segoe UI",system-ui,sans-serif;--body:"Segoe UI",system-ui,sans-serif}}
*{{box-sizing:border-box}}
body{{margin:0;background:var(--bg);color:var(--ink);font:400 15px/1.55 var(--body)}}
main{{max-width:860px;margin:0 auto;padding:40px 20px 64px;display:grid;gap:14px}}
main>*{{min-width:0}}
h1,h2,.titulo{{font-family:var(--display);text-wrap:balance;margin:0;line-height:1.2;color:var(--accent)}}
.titulo{{font-size:30px;font-weight:700}}
h1{{font-size:21px;font-weight:700;margin-top:22px}}
h2{{font-size:17px;font-weight:700;margin-top:10px}}
p{{margin:0;max-width:72ch}}
.meta{{color:var(--muted);font-size:13.5px}}
.nota{{color:var(--muted);font-size:13px}}
ul{{margin:0;padding-left:20px;max-width:72ch;display:grid;gap:6px}}
.tabla{{overflow-x:auto}}
table{{border-collapse:collapse;width:100%;font-size:13.5px}}
caption{{text-align:left;font-weight:700;padding:0 0 6px}}
th,td{{padding:6px 10px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}}
thead th{{background:var(--fill);font-weight:700;border-top:1px solid var(--line)}}
td.n,th.n{{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}}
tfoot td{{font-weight:700;border-top:2px solid var(--line)}}
.grafico{{display:grid;gap:6px}}
.leyenda{{display:flex;flex-wrap:wrap;gap:4px 18px;font-size:13.5px}}
.dot{{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px}}
svg{{display:block;width:100%;height:auto}}
@page{{size:A4;margin:20mm 18mm}}
@media print{{main{{padding:0;max-width:none}}h1,h2{{break-after:avoid}}tr,.grafico{{break-inside:avoid}}}}
"""


def bloque_grafico_html(cfg, r):
    return (f'<div class="grafico"><h2>Horas con rastro por mes</h2><div class="leyenda">{leyenda_html(cfg)}</div>'
            f'{grafico_svg(cfg, r)}</div>')


def html_doc(cfg, r, B):
    out = []
    for tipo, x in B:
        if tipo == "titulo":
            out.append(f'<p class="titulo">{html.escape(x)}</p>')
        elif tipo == "meta":
            out.append(f'<p class="meta">{html.escape(x)}</p>')
        elif tipo in ("h1", "h2"):
            out.append(f"<{tipo}>{html.escape(x)}</{tipo}>")
        elif tipo == "p":
            out.append(f"<p>{negritas_html(x)}</p>")
        elif tipo == "nota":
            out.append(f'<p class="nota">{negritas_html(x)}</p>')
        elif tipo == "lista":
            out.append("<ul>" + "".join(f"<li>{negritas_html(i)}</li>" for i in x) + "</ul>")
        elif tipo == "grafico":
            out.append(bloque_grafico_html(cfg, r))
        elif tipo == "tabla":
            n = set(x["num"])
            cls = lambda i: ' class="n"' if i in n else ""
            t = ['<div class="tabla"><table>']
            if x.get("titulo"):
                t.append(f"<caption>{html.escape(x['titulo'])}</caption>")
            t.append("<thead><tr>" + "".join(f"<th{cls(i)}>{html.escape(c)}</th>" for i, c in enumerate(x["cab"])) + "</tr></thead><tbody>")
            for f in x["filas"]:
                t.append("<tr>" + "".join(f"<td{cls(i)}>{html.escape(c)}</td>" for i, c in enumerate(f)) + "</tr>")
            t.append("</tbody>")
            if x.get("pie"):
                t.append("<tfoot><tr>" + "".join(f"<td{cls(i)}>{html.escape(c)}</td>" for i, c in enumerate(x["pie"])) + "</tr></tfoot>")
            t.append("</table></div>")
            out.append("".join(t))
    return (f'<!DOCTYPE html>\n<html lang="es">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>Tiempo de desarrollo de BioSoft</title>\n<style>{CSS}</style>\n</head>\n<body>\n<main>\n'
            + "\n".join(out) + "\n</main>\n</body>\n</html>\n")


# -------------------------------------------------------------- Chrome ----
def chrome():
    for c in [os.environ.get("CHROME", ""),
              r"C:\Program Files\Google\Chrome\Application\chrome.exe",
              r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
              r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
              shutil.which("chrome") or "", shutil.which("google-chrome") or "", shutil.which("chromium") or ""]:
        if c and Path(c).is_file():
            return c
    return None


def correr_chrome(exe, *args):
    with tempfile.TemporaryDirectory() as ud:
        subprocess.run([exe, "--headless=new", "--disable-gpu", "--no-sandbox", f"--user-data-dir={ud}", *args],
                       capture_output=True, timeout=120)


# ------------------------------------------------------------------ Word ----
NS = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
      'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
      'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
      'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
      'xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"')
ANCHO_TWIPS = 9638  # A4 con 2 cm de margen por lado


def runs(t, extra=""):
    out = []
    for i, parte in enumerate(re.split(r"\*\*(.+?)\*\*", t)):
        if parte:
            b = "<w:b/>" if i % 2 else ""
            out.append(f'<w:r><w:rPr>{b}{extra}</w:rPr><w:t xml:space="preserve">{escape(parte)}</w:t></w:r>')
    return "".join(out)


def par(t, estilo=None, jc=None, extra_r=""):
    ppr = ""
    if estilo or jc:
        ppr = "<w:pPr>" + (f'<w:pStyle w:val="{estilo}"/>' if estilo else "") + (f'<w:jc w:val="{jc}"/>' if jc else "") + "</w:pPr>"
    return f"<w:p>{ppr}{runs(t, extra_r)}</w:p>"


def tabla_docx(x):
    tot = sum(x["anchos"])
    anchos = [round(ANCHO_TWIPS * a / tot) for a in x["anchos"]]
    n = set(x["num"])
    borde = '<w:{0} w:val="single" w:sz="4" w:space="0" w:color="D9DEE3"/>'

    def celda(c, i, cab=False, pie=False):
        shd = '<w:shd w:val="clear" w:color="auto" w:fill="F2F5F8"/>' if cab else ""
        bold = "<w:b/>" if cab or pie else ""
        jc = "right" if i in n else None
        return (f'<w:tc><w:tcPr><w:tcW w:w="{anchos[i]}" w:type="dxa"/>{shd}</w:tcPr>'
                + par(c, "Tabla", jc, bold) + "</w:tc>")

    filas = ['<w:tr><w:trPr><w:cantSplit/><w:tblHeader/></w:trPr>'
             + "".join(celda(c, i, cab=True) for i, c in enumerate(x["cab"])) + "</w:tr>"]
    for f in x["filas"]:
        filas.append("<w:tr><w:trPr><w:cantSplit/></w:trPr>" + "".join(celda(c, i) for i, c in enumerate(f)) + "</w:tr>")
    if x.get("pie"):
        filas.append("<w:tr><w:trPr><w:cantSplit/></w:trPr>" + "".join(celda(c, i, pie=True) for i, c in enumerate(x["pie"])) + "</w:tr>")
    tit = par(x["titulo"], "TituloTabla") if x.get("titulo") else ""
    return (tit + '<w:tbl><w:tblPr><w:tblW w:w="5000" w:type="pct"/><w:tblBorders>'
            + "".join(borde.format(b) for b in ("top", "bottom", "insideH"))
            + '</w:tblBorders><w:tblLayout w:type="fixed"/><w:tblCellMar><w:left w:w="80" w:type="dxa"/>'
              '<w:right w:w="80" w:type="dxa"/></w:tblCellMar><w:tblLook w:val="04A0"/></w:tblPr><w:tblGrid>'
            + "".join(f'<w:gridCol w:w="{a}"/>' for a in anchos) + "</w:tblGrid>" + "".join(filas)
            + "</w:tbl>" + '<w:p><w:pPr><w:pStyle w:val="Tabla"/></w:pPr></w:p>')


def imagen_docx(ancho_px, alto_px):
    cx = 6120000  # 17 cm
    cy = round(cx * alto_px / ancho_px)
    return ('<w:p><w:pPr><w:keepNext/></w:pPr><w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0">'
            f'<wp:extent cx="{cx}" cy="{cy}"/><wp:docPr id="1" name="Grafico" descr="Horas con rastro por mes"/>'
            '<a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:pic>'
            '<pic:nvPicPr><pic:cNvPr id="0" name="grafico.png"/><pic:cNvPicPr/></pic:nvPicPr>'
            '<pic:blipFill><a:blip r:embed="rIdGrafico"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
            f'<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
            '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic>'
            "</wp:inline></w:drawing></w:r></w:p>")


def estilos_docx():
    def est(sid, nombre, ppr="", rpr="", basado="Normal", sig=None, tipo="paragraph"):
        return (f'<w:style w:type="{tipo}" w:styleId="{sid}"><w:name w:val="{nombre}"/>'
                + (f'<w:basedOn w:val="{basado}"/>' if basado else "") + (f'<w:next w:val="{sig}"/>' if sig else "")
                + '<w:qFormat/>' + (f"<w:pPr>{ppr}</w:pPr>" if ppr else "") + (f"<w:rPr>{rpr}</w:rPr>" if rpr else "")
                + "</w:style>")
    fuente = '<w:rFonts w:ascii="Segoe UI" w:hAnsi="Segoe UI" w:cs="Segoe UI"/>'
    disp = '<w:rFonts w:ascii="Segoe UI" w:hAnsi="Segoe UI" w:cs="Segoe UI"/>'
    acento = ACENTO.lstrip("#")
    return (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:styles {NS}>'
            f'<w:docDefaults><w:rPrDefault><w:rPr>{fuente}<w:sz w:val="22"/><w:lang w:val="es-AR"/></w:rPr></w:rPrDefault>'
            '<w:pPrDefault><w:pPr><w:spacing w:after="120" w:line="264" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>'
            + est("Normal", "Normal", basado=None)
            + est("Title", "Title", '<w:spacing w:after="60"/>', f'{disp}<w:b/><w:color w:val="{acento}"/><w:sz w:val="40"/>', sig="Normal")
            + est("Heading1", "heading 1", '<w:keepNext/><w:keepLines/><w:spacing w:before="360" w:after="120"/><w:outlineLvl w:val="0"/>',
                  f'{disp}<w:b/><w:color w:val="{acento}"/><w:sz w:val="30"/>', sig="Normal")
            + est("Heading2", "heading 2", '<w:keepNext/><w:keepLines/><w:spacing w:before="240" w:after="80"/><w:outlineLvl w:val="1"/>',
                  f'{disp}<w:b/><w:color w:val="{acento}"/><w:sz w:val="25"/>', sig="Normal")
            + est("Meta", "Meta", "", '<w:color w:val="535C66"/><w:sz w:val="19"/>')
            + est("Nota", "Nota", '<w:spacing w:after="160"/>', '<w:color w:val="535C66"/><w:sz w:val="19"/>')
            + est("Tabla", "Texto de tabla", '<w:spacing w:before="20" w:after="20" w:line="240" w:lineRule="auto"/>', '<w:sz w:val="19"/>')
            + est("TituloTabla", "Titulo de tabla", '<w:keepNext/><w:spacing w:after="60"/>', '<w:b/><w:sz w:val="20"/>')
            + est("Vineta", "Vineta", '<w:spacing w:after="80"/><w:ind w:left="360" w:hanging="240"/>')
            + "</w:styles>")


def docx(cfg, r, B, destino, png):
    cuerpo = []
    for tipo, x in B:
        if tipo == "titulo":
            cuerpo.append(par(x, "Title"))
        elif tipo == "meta":
            cuerpo.append(par(x, "Meta"))
        elif tipo == "h1":
            cuerpo.append(par(x, "Heading1"))
        elif tipo == "h2":
            cuerpo.append(par(x, "Heading2"))
        elif tipo == "p":
            cuerpo.append(par(x))
        elif tipo == "nota":
            cuerpo.append(par(x, "Nota"))
        elif tipo == "lista":
            cuerpo += [par("•\u00a0\u00a0" + i, "Vineta") for i in x]
        elif tipo == "tabla":
            cuerpo.append(tabla_docx(x))
        elif tipo == "grafico":
            cuerpo.append(par("Horas con rastro por mes", "Heading2"))
            if png:
                cuerpo.append(imagen_docx(*png[1]))
            else:
                cuerpo.append(par("(Gráfico no disponible: se generó sin Chrome. Ver la tabla siguiente.)", "Nota"))
    sect = ('<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1134" w:right="1134" w:bottom="1134" '
            'w:left="1134" w:header="567" w:footer="567" w:gutter="0"/></w:sectPr>')
    documento_xml = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document {NS}><w:body>'
                     + "".join(cuerpo) + sect + "</w:body></w:document>")
    rels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rIdEstilos" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
            + ('<Relationship Id="rIdGrafico" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/grafico.png"/>' if png else "")
            + "</Relationships>")
    with zipfile.ZipFile(destino, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                   '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                   '<Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/>'
                   '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
                   '<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>'
                   "</Types>")
        z.writestr("_rels/.rels",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                   '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
                   "</Relationships>")
        z.writestr("word/document.xml", documento_xml)
        z.writestr("word/styles.xml", estilos_docx())
        z.writestr("word/_rels/document.xml.rels", rels)
        if png:
            z.write(png[0], "word/media/grafico.png")


def tam_png(p):
    b = Path(p).read_bytes()[16:24]
    return int.from_bytes(b[:4], "big"), int.from_bytes(b[4:], "big")


# ------------------------------------------------------------------ main ----
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sin-pdf", action="store_true", help="no usar Chrome (sin PDF ni gráfico en el Word)")
    args = ap.parse_args()

    cfg = json.loads(CONFIG.read_text(encoding="utf8"))
    r = calcular(cfg)
    B = documento(cfg, r)
    SALIDA.mkdir(parents=True, exist_ok=True)
    base = SALIDA / "anexo-tiempo-desarrollo"
    base.with_suffix(".html").write_text(html_doc(cfg, r, B), encoding="utf8")

    png = None
    exe = None if args.sin_pdf else chrome()
    if exe:
        g = SALIDA / "grafico.html"
        g.write_text(f'<!DOCTYPE html><html><head><meta charset="utf-8"><style>{CSS}body{{width:800px}}'
                     f'.grafico{{padding:4px 0}}.grafico h2{{display:none}}</style></head><body>'
                     f'{bloque_grafico_html(cfg, r)}</body></html>', encoding="utf8")
        correr_chrome(exe, "--window-size=800,330", "--force-device-scale-factor=2", "--hide-scrollbars",
                      f"--screenshot={SALIDA / 'grafico.png'}", g.as_uri())
        g.unlink()
        if (SALIDA / "grafico.png").is_file():
            png = (SALIDA / "grafico.png", tam_png(SALIDA / "grafico.png"))
        correr_chrome(exe, "--no-pdf-header-footer", f"--print-to-pdf={base.with_suffix('.pdf')}",
                      base.with_suffix(".html").as_uri())
    elif not args.sin_pdf:
        print("Aviso: no se encontró Chrome ni Edge; sin PDF ni gráfico en el Word (definir CHROME=ruta).")

    docx(cfg, r, B, base.with_suffix(".docx"), png)

    datos = {k: v for k, v in r.items() if k != "corte"}
    datos["corte"] = cfg["corte"]
    (SALIDA / "datos.json").write_text(json.dumps(datos, ensure_ascii=False, indent=1, default=str), encoding="utf8")

    ra, rb = (r["resumen"][e["id"]] for e in cfg["etapas"])
    print(f"Rastros: {r['rastros']}  ·  semanas con rastro: {r['semanas_rastro']}/{r['semanas']}")
    for e in cfg["etapas"]:
        x = r["resumen"][e["id"]]
        print(f"  {e['nombre']}: {x['semanas']:.1f} sem × {e['ritmo']} h = {x['estimadas']:.0f} h estimadas, "
              f"{x['medidas']:.0f} h con rastro, {x['dias']} días")
    print(f"  Total: {ra['estimadas'] + rb['estimadas']:.0f} h estimadas, {ra['medidas'] + rb['medidas']:.0f} h con rastro")
    for p in sorted(SALIDA.iterdir()):
        print(f"  -> {p.relative_to(RAIZ)}")


if __name__ == "__main__":
    sys.exit(main())
