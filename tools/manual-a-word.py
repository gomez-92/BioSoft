#!/usr/bin/env python3
"""Exporta docs/manual-de-usuario.html a docs/manual-de-usuario.docx.

    python tools/manual-a-word.py

Escribe el .docx directamente (OOXML dentro de un ZIP), sin pasar por Word.
Se intento primero automatizar Word por COM y se descarto: devolvia
RPC_E_CALL_REJECTED de forma persistente al guardar un HTML abierto —
sintoma tipico de la Vista Protegida, que abre el archivo en un contexto
donde el objeto Document no esta del todo disponible. Ademas ataba la
exportacion a tener Word instalado. Generando el XML a mano el script corre
en cualquier lado y con la biblioteca estandar sola.

Dos cosas que el HTML del manual no puede dar tal cual, y que este script
resuelve:

1. **La estructura de titulos esta al reves para Word.** En el manual el
   `<h2>` es la etiqueta chica ("Seccion 4") y el titulo real es un
   `<p class="h2title">`. Se ve bien en pantalla, pero el panel de
   navegacion de Word y su tabla de contenido automatica se arman con los
   encabezados: el documento saldria con catorce entradas "Seccion N" y
   ningun titulo. Aca se fusionan en un Titulo 1 numerado.

2. **Word necesita estilos, no CSS.** El .docx trae definidos Titulo 1,
   Titulo 2, Normal y los de lista, asi que el que edite puede cambiar la
   apariencia de todo el documento desde la galeria de estilos, que es como
   se trabaja en Word. Las llamadas (advertencias) llevan sombreado y borde
   izquierdo como formato directo, porque son pocas y puntuales.

El .docx es una EXPORTACION, no una copia sincronizada: si se edita en Word,
volver a correr esto lo pisa. A partir de esa edicion, el .docx es el bueno y
los cambios hay que bajarlos al HTML a mano.
"""

import re
import sys
import zipfile
from html.parser import HTMLParser
from pathlib import Path
import html as _html
from xml.sax.saxutils import escape

RAIZ = Path(__file__).resolve().parent.parent
FUENTE = RAIZ / "docs" / "manual-de-usuario.html"
DESTINO = RAIZ / "docs" / "manual-de-usuario.docx"

W = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"'

# Paleta: los mismos colores del manual, resueltos (Word no entiende var()).
TEAL, WARN, STOP = "0B4A50", "6D4100", "7D2018"
TEAL_BG, WARN_BG, STOP_BG = "EEF4F4", "FBF4E6", "FBEEEC"
GRIS, RAYA = "5A626B", "DFE4EA"


# ---------------------------------------------------------------- inline ----
class _Inline(HTMLParser):
    """Convierte un fragmento HTML en una lista de runs (texto + formato)."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.runs = []
        self.b = self.i = self.c = 0

    def handle_starttag(self, tag, attrs):
        if tag in ("strong", "b"):
            self.b += 1
        elif tag in ("em", "i"):
            self.i += 1
        elif tag == "code":
            self.c += 1
        elif tag == "br":
            self.runs.append(("\n", False, False, False))

    def handle_endtag(self, tag):
        if tag in ("strong", "b"):
            self.b = max(0, self.b - 1)
        elif tag in ("em", "i"):
            self.i = max(0, self.i - 1)
        elif tag == "code":
            self.c = max(0, self.c - 1)

    def handle_data(self, data):
        data = re.sub(r"\s+", " ", data)
        if data:
            self.runs.append((data, self.b > 0, self.i > 0, self.c > 0))


def runs_xml(fragmento):
    p = _Inline()
    p.feed(fragmento)
    out = []
    for texto, b, i, c in p.runs:
        if texto == "\n":
            out.append("<w:r><w:br/></w:r>")
            continue
        props = ""
        if b:
            props += "<w:b/>"
        if i:
            props += "<w:i/>"
        if c:
            props += '<w:rFonts w:ascii="Consolas" w:hAnsi="Consolas"/><w:sz w:val="19"/>'
        props = "<w:rPr>%s</w:rPr>" % props if props else ""
        out.append('<w:r>%s<w:t xml:space="preserve">%s</w:t></w:r>'
                   % (props, escape(texto)))
    return "".join(out)


def parrafo(fragmento, estilo=None, extra_pr=""):
    pr = ""
    if estilo:
        pr += '<w:pStyle w:val="%s"/>' % estilo
    pr += extra_pr
    pr = "<w:pPr>%s</w:pPr>" % pr if pr else ""
    return "<w:p>%s%s</w:p>" % (pr, runs_xml(fragmento))


# Formato directo de las llamadas: sombreado + borde izquierdo grueso.
def _caja(color, fondo):
    return ('<w:pBdr><w:left w:val="single" w:sz="18" w:space="8" w:color="%s"/></w:pBdr>'
            '<w:shd w:val="clear" w:fill="%s"/>'
            '<w:ind w:left="200" w:right="120"/>' % (color, fondo))


# -------------------------------------------------------------- diagramas ----
# Los diagramas de cableado son texto monoespaciado con caracteres de caja:
# cada linea es un run separado por <w:br/>, sin colapsar espacios (que es
# lo que hace _Inline y arruinaria la alineacion). Un solo parrafo con
# keepLines para que Word no lo parta entre paginas.
def diagrama_xml(texto):
    lineas = _html.unescape(texto).strip("\n").split("\n")
    runs = []
    for i, linea in enumerate(lineas):
        if i:
            runs.append("<w:r><w:br/></w:r>")
        runs.append('<w:r><w:t xml:space="preserve">%s</w:t></w:r>' % escape(linea))
    return ('<w:p><w:pPr><w:pStyle w:val="Diagrama"/><w:keepLines/></w:pPr>%s</w:p>'
            % "".join(runs))


# ----------------------------------------------------------------- tablas ----
def tabla_xml(html_tabla):
    filas = re.findall(r"<tr>(.*?)</tr>", html_tabla, re.S)
    if not filas:
        return ""
    ncols = max(len(re.findall(r"<t[hd][^>]*>", f)) for f in filas)
    ancho = int(9360 / ncols)

    borde = ('<w:tblBorders>'
             '<w:top w:val="single" w:sz="4" w:color="%s"/>'
             '<w:bottom w:val="single" w:sz="4" w:color="%s"/>'
             '<w:insideH w:val="single" w:sz="4" w:color="%s"/>'
             '</w:tblBorders>' % (RAYA, RAYA, RAYA))

    out = ['<w:tbl><w:tblPr><w:tblW w:w="5000" w:type="pct"/>%s'
           '<w:tblCellMar><w:top w:w="60" w:type="dxa"/><w:bottom w:w="60" w:type="dxa"/>'
           '</w:tblCellMar></w:tblPr><w:tblGrid>%s</w:tblGrid>'
           % (borde, '<w:gridCol w:w="%d"/>' % ancho * ncols)]

    for fila in filas:
        celdas = re.findall(r"<t([hd])[^>]*>(.*?)</t[hd]>", fila, re.S)
        encabezado = celdas and celdas[0][0] == "h"
        tr = ['<w:tr>']
        if encabezado:
            tr.insert(1, "<w:trPr><w:tblHeader/></w:trPr>")
        for j, (_, contenido) in enumerate(celdas):
            estilo = "TablaEncabezado" if encabezado else "TablaCelda"
            # La primera columna es la que identifica la fila: va en negrita,
            # igual que en el HTML.
            frag = contenido.strip()
            if not encabezado and j == 0:
                frag = "<strong>%s</strong>" % frag
            tr.append('<w:tc><w:tcPr><w:tcW w:w="%d" w:type="dxa"/></w:tcPr>%s</w:tc>'
                      % (ancho, parrafo(frag, estilo)))
        tr.append("</w:tr>")
        out.append("".join(tr))

    out.append("</w:tbl>")
    # Word necesita un parrafo despues de una tabla para poder escribir ahi.
    out.append('<w:p><w:pPr><w:spacing w:after="0" w:line="120" w:lineRule="exact"/></w:pPr></w:p>')
    return "".join(out)


# ------------------------------------------------------------------ cuerpo ---
def construir_cuerpo():
    html = FUENTE.read_text(encoding="utf-8")
    m = re.search(r"<article>(.*?)</article>", html, re.S)
    if not m:
        sys.exit("No se encontro el <article> en " + str(FUENTE))
    cuerpo = m.group(1)

    # El indice impreso no va: Word arma el suyo desde los encabezados
    # (Referencias > Tabla de contenido) y ese si se actualiza al editar.
    cuerpo = re.sub(r'<section class="print-toc">.*?</section>', "", cuerpo, flags=re.S)

    # "Seccion N" + titulo -> un solo Titulo 1 numerado.
    cuerpo = re.sub(
        r'<h2 id="s\d+">Secci[oó]n\s*(\d+)</h2>\s*<p class="h2title">(.*?)</p>',
        lambda x: "<h1>%s. %s</h1>" % (x.group(1), x.group(2).strip()),
        cuerpo, flags=re.S)

    piezas = []

    # Portada.
    piezas.append(parrafo("BioSoft", "Title"))
    piezas.append(parrafo(
        "Manual de usuario — sistema de exposición controlada a campos "
        "electromagnéticos de frecuencia extremadamente baja para "
        "experimentación con animales de laboratorio.", "Subtitle"))
    piezas.append(parrafo(
        "Destinatario: operador y personal técnico del equipo   ·   "
        "Versión 2, septiembre de 2026   ·   Estado: equipo en desarrollo",
        "Metadatos"))

    # Recorrido secuencial de los bloques de primer nivel.
    patron = re.compile(
        r"<h1>(?P<h1>.*?)</h1>"
        r"|<h3>(?P<h3>.*?)</h3>"
        r"|<div class=\"callout(?P<cls> \w+)?\">(?P<call>.*?)</div>"
        r"|<div class=\"term\">(?P<term>.*?)</div>"
        r"|<table[^>]*>(?P<tab>.*?)</table>"
        r"|<ul>(?P<ul>.*?)</ul>"
        r"|<ol>(?P<ol>.*?)</ol>"
        r"|<pre class=\"diagram\">(?P<pre>.*?)</pre>"
        r"|<figcaption>(?P<figcap>.*?)</figcaption>"
        # `<p` seguido de espacio o `>`: sin esa restriccion, `<pre` matchea
        # como un <p> con atributos "re ..." y el diagrama se pierde.
        r"|<p(?P<pattr>\s[^>]*|)>(?P<p>.*?)</p>", re.S)

    for x in patron.finditer(cuerpo):
        if x.group("h1") is not None:
            piezas.append(parrafo(x.group("h1"), "Heading1"))
        elif x.group("h3") is not None:
            piezas.append(parrafo(x.group("h3"), "Heading2"))
        elif x.group("call") is not None:
            cls = (x.group("cls") or "").strip()
            color, fondo = {"warn": (WARN, WARN_BG), "stop": (STOP, STOP_BG)}.get(
                cls, (TEAL, TEAL_BG))
            bloque = x.group("call")
            tag = re.search(r'<span class="tag">(.*?)</span>', bloque, re.S)
            if tag:
                piezas.append(parrafo(tag.group(1), "Etiqueta",
                                      _caja(color, fondo) +
                                      '<w:spacing w:before="180" w:after="40"/>'))
            for p in re.findall(r"<p>(.*?)</p>", bloque, re.S):
                piezas.append(parrafo(p, "Llamada", _caja(color, fondo)))
        elif x.group("term") is not None:
            piezas.append(parrafo(x.group("term").replace("<br>", " — "), "Termino"))
        elif x.group("tab") is not None:
            piezas.append(tabla_xml(x.group("tab")))
        elif x.group("ul") is not None:
            for li in re.findall(r"<li>(.*?)</li>", x.group("ul"), re.S):
                piezas.append(parrafo(li, "Vinieta"))
        elif x.group("ol") is not None:
            for li in re.findall(r"<li>(.*?)</li>", x.group("ol"), re.S):
                piezas.append(parrafo(li, "Numerada"))
        elif x.group("pre") is not None:
            piezas.append(diagrama_xml(x.group("pre")))
        elif x.group("figcap") is not None:
            piezas.append(parrafo(x.group("figcap"), "Epigrafe"))
        elif x.group("p") is not None:
            if "h2title" in (x.group("pattr") or ""):
                continue   # ya absorbido en el Titulo 1
            piezas.append(parrafo(x.group("p")))

    return "".join(piezas)


# ------------------------------------------------------------------ estilos --
def _estilo(sid, nombre, basado, ppr, rpr, siguiente=None):
    return ('<w:style w:type="paragraph" w:styleId="%s"><w:name w:val="%s"/>'
            '<w:basedOn w:val="%s"/><w:next w:val="%s"/><w:qFormat/>'
            '<w:pPr>%s</w:pPr><w:rPr>%s</w:rPr></w:style>'
            % (sid, nombre, basado, siguiente or "Normal", ppr, rpr))


SERIF = '<w:rFonts w:ascii="Georgia" w:hAnsi="Georgia"/>'
SANS = '<w:rFonts w:ascii="Segoe UI" w:hAnsi="Segoe UI"/>'


def styles_xml():
    s = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
         '<w:styles %s>' % W,
         '<w:docDefaults><w:rPrDefault><w:rPr>%s<w:sz w:val="22"/></w:rPr></w:rPrDefault>'
         '<w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="280" w:lineRule="atLeast"/>'
         '</w:pPr></w:pPrDefault></w:docDefaults>' % SERIF,
         '<w:style w:type="paragraph" w:default="1" w:styleId="Normal">'
         '<w:name w:val="Normal"/><w:qFormat/></w:style>']

    # Titulo 1 = seccion. Salto de pagina antes: el manual se consulta
    # salteado, asi que cada seccion arranca en hoja nueva, igual que el PDF.
    s.append(_estilo(
        "Heading1", "heading 1", "Normal",
        '<w:keepNext/><w:pageBreakBefore/><w:spacing w:before="0" w:after="200"/>'
        '<w:outlineLvl w:val="0"/>',
        SANS + '<w:b/><w:sz w:val="40"/><w:color w:val="%s"/>' % TEAL))
    s.append(_estilo(
        "Heading2", "heading 2", "Normal",
        '<w:keepNext/><w:spacing w:before="280" w:after="80"/><w:outlineLvl w:val="1"/>',
        SANS + '<w:b/><w:sz w:val="26"/>'))
    s.append(_estilo(
        "Title", "Title", "Normal",
        '<w:spacing w:before="2600" w:after="60"/>',
        SANS + '<w:b/><w:sz w:val="72"/><w:color w:val="%s"/>' % TEAL))
    s.append(_estilo(
        "Subtitle", "Subtitle", "Normal", '<w:spacing w:after="280"/>',
        SERIF + '<w:sz w:val="28"/><w:color w:val="3D454E"/>'))
    s.append(_estilo(
        "Metadatos", "Metadatos", "Normal",
        '<w:pBdr><w:top w:val="single" w:sz="4" w:space="6" w:color="%s"/></w:pBdr>'
        '<w:spacing w:after="0"/>' % RAYA,
        SANS + '<w:sz w:val="18"/><w:color w:val="%s"/>' % GRIS))
    s.append(_estilo(
        "Llamada", "Llamada", "Normal",
        '<w:spacing w:before="0" w:after="60"/>', SERIF + '<w:sz w:val="21"/>'))
    s.append(_estilo(
        "Etiqueta", "Etiqueta de llamada", "Normal", '<w:spacing w:after="40"/>',
        SANS + '<w:b/><w:caps/><w:sz w:val="16"/><w:spacing w:val="20"/>'))
    s.append(_estilo(
        "Termino", "Termino", "Normal",
        '<w:pBdr><w:left w:val="single" w:sz="8" w:space="8" w:color="%s"/></w:pBdr>'
        '<w:ind w:left="180"/><w:spacing w:after="120"/>' % RAYA,
        SERIF))
    s.append(_estilo(
        "Diagrama", "Diagrama", "Normal",
        '<w:pBdr><w:top w:val="single" w:sz="4" w:space="6" w:color="%s"/>'
        '<w:left w:val="single" w:sz="4" w:space="6" w:color="%s"/>'
        '<w:bottom w:val="single" w:sz="4" w:space="6" w:color="%s"/>'
        '<w:right w:val="single" w:sz="4" w:space="6" w:color="%s"/></w:pBdr>'
        '<w:shd w:val="clear" w:fill="F0F2F4"/><w:spacing w:after="60" w:line="240" w:lineRule="auto"/>'
        % (RAYA, RAYA, RAYA, RAYA),
        '<w:rFonts w:ascii="Consolas" w:hAnsi="Consolas" w:cs="Consolas"/><w:sz w:val="14"/>'))
    s.append(_estilo(
        "Epigrafe", "Epigrafe", "Normal", '<w:spacing w:after="200"/>',
        SANS + '<w:sz w:val="18"/><w:color w:val="%s"/>' % GRIS))
    s.append(_estilo(
        "TablaEncabezado", "Tabla encabezado", "Normal",
        '<w:spacing w:after="0"/><w:keepNext/>',
        SANS + '<w:b/><w:caps/><w:sz w:val="16"/><w:color w:val="%s"/>' % GRIS))
    s.append(_estilo(
        "TablaCelda", "Tabla celda", "Normal", '<w:spacing w:after="0"/>',
        SANS + '<w:sz w:val="19"/>'))
    s.append(_estilo(
        "Vinieta", "Vineta BioSoft", "Normal",
        '<w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr>'
        '<w:spacing w:after="80"/><w:ind w:left="360" w:hanging="200"/>', ""))
    s.append(_estilo(
        "Numerada", "Numerada BioSoft", "Normal",
        '<w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr>'
        '<w:spacing w:after="80"/><w:ind w:left="360" w:hanging="200"/>', ""))
    s.append("</w:styles>")
    return "".join(s)


def numbering_xml():
    def abstracto(aid, fmt, texto):
        return ('<w:abstractNum w:abstractNumId="%d"><w:lvl w:ilvl="0">'
                '<w:start w:val="1"/><w:numFmt w:val="%s"/><w:lvlText w:val="%s"/>'
                '<w:lvlJc w:val="left"/><w:pPr><w:ind w:left="360" w:hanging="200"/></w:pPr>'
                '%s</w:lvl></w:abstractNum>'
                % (aid, fmt, texto,
                   '<w:rPr><w:rFonts w:ascii="Symbol" w:hAnsi="Symbol"/></w:rPr>'
                   if fmt == "bullet" else ""))
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
            '<w:numbering %s>%s%s'
            '<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>'
            '<w:num w:numId="2"><w:abstractNumId w:val="1"/></w:num>'
            '</w:numbering>'
            % (W, abstracto(0, "bullet", ""), abstracto(1, "decimal", "%1.")))


def document_xml(cuerpo):
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
            '<w:document %s><w:body>%s'
            '<w:sectPr><w:pgSz w:w="11906" w:h="16838"/>'
            '<w:pgMar w:top="1134" w:right="1021" w:bottom="1134" w:left="1021"'
            ' w:header="709" w:footer="709" w:gutter="0"/></w:sectPr>'
            '</w:body></w:document>' % (W, cuerpo))


CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
<Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/>
</Types>"""

RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

DOC_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" Target="numbering.xml"/>
</Relationships>"""


def main():
    cuerpo = construir_cuerpo()

    # Fecha fija en cada entrada del ZIP. writestr() con un nombre suelto
    # estampa la hora actual, asi que dos corridas seguidas del mismo contenido
    # dan archivos distintos byte a byte: el .docx esta versionado, y un binario
    # que aparece modificado cada vez que alguien corre el script ensucia el
    # diff y esconde los cambios que si importan.
    fecha = (1980, 1, 1, 0, 0, 0)

    def escribir(z, nombre, contenido):
        info = zipfile.ZipInfo(nombre, date_time=fecha)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o600 << 16
        z.writestr(info, contenido)

    with zipfile.ZipFile(DESTINO, "w", zipfile.ZIP_DEFLATED) as z:
        escribir(z, "[Content_Types].xml", CONTENT_TYPES)
        escribir(z, "_rels/.rels", RELS)
        escribir(z, "word/_rels/document.xml.rels", DOC_RELS)
        escribir(z, "word/document.xml", document_xml(cuerpo))
        escribir(z, "word/styles.xml", styles_xml())
        escribir(z, "word/numbering.xml", numbering_xml())
    print("generado:", DESTINO, "(%.0f KB)" % (DESTINO.stat().st_size / 1024))


if __name__ == "__main__":
    main()
