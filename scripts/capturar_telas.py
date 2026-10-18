#!/usr/bin/env python3
"""Copia o conjunto de telas do radar pela serial USB.

Roda na placa que ja esta com o firmware, depois de compilar e gravar.
Porta padrao: COM11. No WSL o script continua no Python do Windows,
porque a COM fica do lado de la.

O alcance e o tema gravados na NVS mudam no meio do percurso.
No fim os dois voltam ao que a placa tinha no primeiro quadro.
Em cada radar, prende uma aeronave que ja tenha foto, se houver.
Nas telas de tema o alcance fica em 30 NM e, se existir, a aeronave
e da companhia daquele tema.
"""

import glob
import math
import os
import subprocess
import sys
import time
from pathlib import Path

W = 800
H = 480
PORT_PADRAO = "COM11"
TENTATIVAS = 3

# Anel e acento, na ordem Gol, LATAM, Azul, Generico. RGB do fonte.
ANEIS = (
    (0x5A, 0x3A, 0x24),
    (0x6A, 0x18, 0x30),
    (0x0E, 0x3F, 0x6E),
    (0x1C, 0x9A, 0x30),
)
ACENTOS = (
    (0xFF, 0x70, 0x20),
    (0xE8, 0x18, 0x48),
    (0x00, 0x57, 0xB8),
    (0x39, 0xFF, 0x14),
)
NOMES_TEMA = ("gol", "latam", "azul", "generico")
CIAS = {
    "gol": ("GLO",),
    "latam": ("TAM", "LAN", "LPE", "LNE", "LXP", "LAP", "ARE", "LCO", "LTG", "LAE"),
    "azul": ("AZU", "ACN"),
    "generico": (),
}
PASSOS_NM = (1, 2, 5, 10, 15, 30, 50, 100, 250)

# Painel do radar em ui_internal.h. O texto do alcance fica dentro do anel.
CX = 8 + 215
CY = 56 + 190
RAIO = 154
CAIXA_ALCANCE = (CX - 48, CY + RAIO - 26 - 10, CX + 48, CY + RAIO - 26 + 14)


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


ANEIS_565 = tuple(rgb565(c) for c in ANEIS)
ACENTOS_565 = tuple(rgb565(c) for c in ACENTOS)


def perto(a, b, tol=4):
    ar, ag, ab = (a >> 11) & 31, (a >> 5) & 63, a & 31
    br, bg, bb = (b >> 11) & 31, (b >> 5) & 63, b & 31
    return abs(ar - br) <= tol and abs(ag - bg) <= tol and abs(ab - bb) <= tol


def pixel(im, x, y):
    r, g, b = im.getpixel((x, y))[:3]
    return rgb565((r, g, b))


def ponto(raio, graus):
    rad = math.radians(graus)
    return (
        int(round(CX + raio * math.sin(rad))),
        int(round(CY - raio * math.cos(rad))),
    )


def achar_tema(im):
    votos = [0, 0, 0, 0]
    for graus in (20, 40, 55, 70, 110, 130, 150, 200, 230, 250, 290, 320):
        x, y = ponto(RAIO, graus)
        if not (0 <= x < W and 0 <= y < H):
            continue
        cor = pixel(im, x, y)
        for i, anel in enumerate(ANEIS_565):
            if perto(cor, anel, 6):
                votos[i] += 1
                break
    melhor = max(range(4), key=lambda i: votos[i])
    if votos[melhor] == 0:
        return 3
    return melhor


def raio_interno(im, anel):
    achados = []
    for graus in (25, 40, 55, 70, 110, 130, 150, 205, 220, 235, 250, 290, 310, 330):
        for raio in range(20, RAIO - 8):
            x, y = ponto(raio, graus)
            if not (0 <= x < W and 0 <= y < H):
                continue
            if not perto(pixel(im, x, y), anel, 5):
                continue
            # A varredura e um bloco continuo. O anel interno e um traco fino.
            x2, y2 = ponto(raio + 4, graus)
            if 0 <= x2 < W and 0 <= y2 < H and perto(pixel(im, x2, y2), anel, 5):
                continue
            achados.append(raio)
            break
    if len(achados) < 2:
        return None
    achados.sort()
    meio = achados[len(achados) // 2]
    perto_meio = [r for r in achados if abs(r - meio) <= 4]
    if len(perto_meio) < 2:
        return None
    return meio


def largura_texto(im, acento):
    x0, y0, x1, y1 = CAIXA_ALCANCE
    colunas = []
    for x in range(x0, x1):
        tinta = False
        for y in range(y0, y1):
            if perto(pixel(im, x, y), acento, 8):
                tinta = True
                break
        colunas.append(tinta)
    if not any(colunas):
        return 0, 0
    primeiro = colunas.index(True)
    ultimo = len(colunas) - 1 - colunas[::-1].index(True)
    # Largura do primeiro glifo, ate a primeira coluna vazia.
    fim = primeiro
    while fim < len(colunas) and colunas[fim]:
        fim += 1
    return ultimo - primeiro + 1, fim - primeiro


def candidatos_alcance(raio, largura, glifo):
    if largura <= 0:
        faixa = [30, 15, 10, 50, 100, 250, 5, 2, 1]
    elif largura < 38:
        faixa = [5, 2, 1]
    elif largura < 48:
        # 10 e 30 cabem no mesmo raio. O 1 e estreito; o 3 ocupa mais colunas.
        faixa = [30, 15, 50, 10] if glifo > 5 else [10, 15, 50, 30]
    else:
        faixa = [100, 250]
    if raio is None:
        return faixa
    esperados = []
    for i in range(1, len(PASSOS_NM)):
        previsto = RAIO * PASSOS_NM[i - 1] / PASSOS_NM[i]
        esperados.append((abs(raio - previsto), PASSOS_NM[i]))
    esperados.sort()
    pelo_raio = [nm for dist, nm in esperados if dist <= 6]
    if not pelo_raio:
        return faixa
    ambos = [nm for nm in faixa if nm in pelo_raio]
    return ambos or faixa


def texto_confere(original, atual, acento):
    x0, y0, x1, y1 = CAIXA_ALCANCE
    total = 0
    iguais = 0
    varredura = 0
    area = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            area += 1
            era = perto(pixel(original, x, y), acento, 8)
            agora = perto(pixel(atual, x, y), acento, 8)
            if era:
                total += 1
                if agora:
                    iguais += 1
            elif agora:
                varredura += 1
    if total < 8:
        return None
    if varredura > area * 0.35:
        return None
    return iguais / total


def quadro_para_png(raw, w, h):
    from PIL import Image

    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = raw[2 * i] | (raw[2 * i + 1] << 8)
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        rgb[3 * i] = (r << 3) | (r >> 2)
        rgb[3 * i + 1] = (g << 2) | (g >> 4)
        rgb[3 * i + 2] = (b << 3) | (b >> 2)
    return Image.frombytes("RGB", (w, h), bytes(rgb))


class Serial:
    def __init__(self, porta):
        import serial

        self.ser = serial.Serial()
        self.ser.port = porta
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        self.ser.write_timeout = 2
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.ser.dtr = False
        self.ser.rts = False
        self.buf = bytearray()

    def close(self):
        self.ser.close()

    def enviar(self, texto):
        self.ser.write((texto + "\n").encode("ascii"))
        self.ser.flush()

    def _puxar(self):
        n = self.ser.in_waiting
        pedaco = self.ser.read(n if n else 1)
        if pedaco:
            self.buf += pedaco
        return bool(pedaco)

    def esperar(self, segundos):
        limite = time.monotonic() + segundos
        while time.monotonic() < limite:
            self._puxar()

    def limpar(self):
        self.esperar(0.15)
        self.buf.clear()
        self.ser.reset_input_buffer()

    def ler_linha(self, limite):
        while True:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                linha = bytes(self.buf[:nl]).decode("ascii", "replace").strip("\r")
                del self.buf[: nl + 1]
                return linha
            if time.monotonic() > limite:
                raise TimeoutError("linha incompleta")
            if not self._puxar():
                time.sleep(0.01)

    def ler_exato(self, n, limite):
        while len(self.buf) < n:
            if time.monotonic() > limite:
                raise TimeoutError("quadro incompleto (%d/%d)" % (len(self.buf), n))
            if not self._puxar():
                time.sleep(0.01)
        saida = bytes(self.buf[:n])
        del self.buf[:n]
        return saida

    def ate_marca(self, marca, limite):
        while True:
            idx = self.buf.find(marca)
            if idx >= 0:
                del self.buf[:idx]
                return
            if time.monotonic() > limite:
                raise TimeoutError("sem %s" % marca.decode("ascii", "replace"))
            if not self._puxar():
                time.sleep(0.01)

    def quadro(self, espera):
        limite = time.monotonic() + espera
        self.ate_marca(b"@@SHOT ", limite)
        fim = self.buf.find(b"\n")
        while fim < 0:
            if time.monotonic() > limite:
                raise TimeoutError("cabecalho sem fim")
            self._puxar()
            fim = self.buf.find(b"\n")
        linha = bytes(self.buf[:fim])
        del self.buf[: fim + 1]
        partes = linha.split()
        if len(partes) != 3:
            raise RuntimeError("cabecalho %r" % linha)
        w, h = int(partes[1]), int(partes[2])
        if w < 1 or h < 1 or w > 2000 or h > 2000:
            raise RuntimeError("tamanho %d %d" % (w, h))
        row_bytes = w * 2
        bruto = bytearray()
        for y in range(h):
            cab = self.ler_exato(9, limite)
            esperado = ("@@R %04d\n" % y).encode("ascii")
            if cab != esperado:
                raise RuntimeError("linha %d veio %r" % (y, cab))
            row = self.ler_exato(row_bytes, limite)
            xor = self.ler_exato(1, limite)[0]
            soma = 0
            for b in row:
                soma ^= b
            if soma != xor:
                raise RuntimeError("xor da linha %d" % y)
            bruto += row
        fim_quadro = self.ler_exato(7, limite)
        if fim_quadro != b"\n@@END\n":
            raise RuntimeError("fim %r" % fim_quadro)
        return w, h, bytes(bruto)


def capturar(serial, nome, pausa):
    from PIL import Image

    ultimo = None
    for tentativa in range(1, TENTATIVAS + 1):
        serial.limpar()
        serial.enviar("shot")
        try:
            w, h, bruto = serial.quadro(12)
            im = quadro_para_png(bruto, w, h)
            if w != W or h != H:
                raise RuntimeError("tamanho %dx%d" % (w, h))
            return im
        except (TimeoutError, RuntimeError) as exc:
            ultimo = exc
            print("  %s tentativa %d: %s" % (nome, tentativa, exc), flush=True)
            time.sleep(0.4)
    raise RuntimeError("%s: %s" % (nome, ultimo))


def comando(serial, texto, pausa):
    serial.limpar()
    serial.enviar(texto)
    serial.esperar(pausa)


def salvar(im, pasta, nome):
    destino = pasta / nome
    im.save(destino, "PNG")
    print("%s  %dx%d" % (nome, im.size[0], im.size[1]), flush=True)
    return destino


def botoes_zoom(im):
    """Os dois botoes ficam dentro do painel, espelhados, fora do anel."""
    esquerda = (63, 382, 65, 33)
    direita = (318, 382, 65, 33)
    painel = (8, 56, 438, 436)

    def tinta(caixa):
        x, y, w, h = caixa
        n = 0
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                r, g, b = im.getpixel((xx, yy))[:3]
                if r + g + b > 48:
                    n += 1
        return n

    if tinta(esquerda) < 20 or tinta(direita) < 20:
        return "botao de zoom apagado ou cortado"
    for caixa in (esquerda, direita):
        x, y, w, h = caixa
        if x < painel[0] or y < painel[1] or x + w > painel[2] or y + h > painel[3]:
            return "botao de zoom fora do painel"
    return ""


def estado_inicial(im):
    tema = achar_tema(im)
    anel = ANEIS_565[tema]
    acento = ACENTOS_565[tema]
    raio = raio_interno(im, anel)
    largura, glifo = largura_texto(im, acento)
    ordem = candidatos_alcance(raio, largura, glifo)
    print(
        "placa: tema %s, anel interno %s, texto %d px, glifo %d -> %s"
        % (
            NOMES_TEMA[tema],
            "?" if raio is None else str(raio),
            largura,
            glifo,
            ", ".join(str(n) for n in ordem),
        ),
        flush=True,
    )
    return tema, ordem, raio, largura


def confirmar_alcance(serial, original, tema, ordem):
    acento = ACENTOS_565[tema]
    melhor_nm = ordem[0]
    melhor_nota = -1.0
    for nm in ordem:
        comando(serial, "nm %d" % nm, 0.45)
        comando(serial, "radar", 0.6)
        atual = capturar(serial, "confere", 0)
        nota = texto_confere(original, atual, acento)
        valor = -1.0 if nota is None else nota
        print("  alcance %d NM, texto %.2f" % (nm, valor), flush=True)
        if valor > melhor_nota:
            melhor_nota = valor
            melhor_nm = nm
        if valor >= 0.72:
            return nm
    comando(serial, "nm %d" % melhor_nm, 0.4)
    return melhor_nm


def python_windows():
    achados = sorted(glob.glob("/mnt/c/Users/*/AppData/Local/Programs/Python/Python3*/python.exe"))
    achados = [p for p in achados if "WindowsApps" not in p]
    if not achados:
        return None
    return achados[-1]


def relancar_no_windows(porta, pasta, extras):
    if os.name == "nt" or not porta.upper().startswith("COM"):
        return
    py = python_windows()
    if not py:
        sys.exit("Python do Windows nao encontrado para abrir %s" % porta)
    script = subprocess.check_output(["wslpath", "-w", str(Path(__file__).resolve())], text=True).strip()
    saida = subprocess.check_output(["wslpath", "-w", str(pasta)], text=True).strip()
    argv = [py, script, "--port", porta, "--out", saida, "--in-windows", *extras]
    os.execv(py, argv)


class Aviao(object):
    def __init__(self, hx, icao, dist, photo):
        self.hex = hx
        self.icao = icao
        self.dist = dist
        self.photo = photo


def listar(serial):
    serial.limpar()
    serial.enviar("lista")
    limite = time.monotonic() + 5
    try:
        serial.ate_marca(b"@@LIST", limite)
        serial.ler_linha(limite)
    except TimeoutError:
        print("  lista sem resposta", flush=True)
        return []
    linhas = []
    while True:
        try:
            linha = serial.ler_linha(limite)
        except TimeoutError:
            break
        if linha.startswith("@@ENDLIST"):
            break
        if not linha.startswith("@@A "):
            continue
        partes = linha.split()
        if len(partes) < 5:
            continue
        icao = "" if partes[2] == "-" else partes[2]
        try:
            dist = int(partes[3])
        except ValueError:
            continue
        linhas.append(Aviao(partes[1], icao, dist, partes[4] == "1"))
    return linhas


def mais_perto(lista):
    return min(lista, key=lambda p: (p.dist, p.hex))


def escolher(lista, cias):
    if not lista:
        return None
    if cias:
        da_cia = [p for p in lista if p.icao in cias]
        com_foto = [p for p in da_cia if p.photo]
        if com_foto:
            return mais_perto(com_foto)
        if da_cia:
            return mais_perto(da_cia)
    com_foto = [p for p in lista if p.photo]
    if com_foto:
        return mais_perto(com_foto)
    return mais_perto(lista)


def esperar_foto(serial, hx, teto):
    limite = time.monotonic() + teto
    while time.monotonic() < limite:
        time.sleep(1.0)
        for p in listar(serial):
            if p.hex == hx and p.photo:
                return True
    return False


def esperar_lista(serial, teto):
    base = listar(serial)
    melhor = base
    limite = time.monotonic() + teto
    repetido = 0
    anterior = len(base)
    while time.monotonic() < limite:
        time.sleep(1.0)
        agora = listar(serial)
        if len(agora) >= len(melhor):
            melhor = agora
        if len(agora) > len(base) and len(agora) == anterior:
            repetido += 1
            if repetido >= 1:
                return agora
        else:
            repetido = 0
        anterior = len(agora)
    return melhor


def ir_alcance(serial, nm, atual):
    if nm != atual:
        comando(serial, "nm %d" % nm, 0.35)
    comando(serial, "radar", 0.45)
    if nm > atual:
        return esperar_lista(serial, 8.0), nm
    return listar(serial), nm


def tem_foto(im):
    x0 = 446 + 24
    y0 = 56 + 174 + 16
    diferentes = 0
    total = 0
    for y in range(y0, y0 + 170, 8):
        for x in range(x0, x0 + 290, 8):
            if x >= W or y >= H:
                continue
            total += 1
            r, g, b = im.getpixel((x, y))[:3]
            if abs(r - 28) + abs(g - 28) + abs(b - 28) > 36:
                diferentes += 1
    return total > 0 and diferentes > total * 0.2


def fila_escolha(lista, cias):
    if cias:
        da_cia = sorted((p for p in lista if p.icao in cias), key=lambda p: p.dist)
        com = [p for p in da_cia if p.photo]
        sem = [p for p in da_cia if not p.photo]
        if com or sem:
            return com + sem
    com = sorted((p for p in lista if p.photo), key=lambda p: p.dist)
    if com:
        return com[:1]
    if not lista:
        return []
    return [mais_perto(lista)]


def quadro_radar(serial, pasta, nome, lista, cias):
    fila = fila_escolha(lista, cias)
    if not fila:
        print("  %s sem aeronave" % nome, flush=True)
    tentados = []
    for i in range(3):
        if not fila:
            break
        escolha = fila[0]
        print(
            "  %s %s %s %s %.1f NM"
            % (
                nome,
                escolha.hex,
                escolha.icao or "-",
                "foto" if escolha.photo else "sem foto ainda",
                escolha.dist / 10.0,
            ),
            flush=True,
        )
        comando(serial, "sel " + escolha.hex, 0.35)
        if escolha.photo or esperar_foto(serial, escolha.hex, 36 if i == 0 else 14):
            if not escolha.photo:
                print("  foto pronta", flush=True)
            serial.esperar(0.55)
            break
        print("  foto nao chegou", flush=True)
        tentados.append(escolha.hex)
        lista = [p for p in listar(serial) if p.hex not in tentados]
        fila = fila_escolha(lista, cias)
    im = capturar(serial, nome, 0)
    salvar(im, pasta, nome)
    aviso = botoes_zoom(im)
    if aviso:
        print("  aviso: %s" % aviso, flush=True)
    if not tem_foto(im):
        print("  aviso: cartao sem foto", flush=True)
    return im


def main():
    porta = PORT_PADRAO
    pasta = Path(__file__).resolve().parent.parent / "telas"
    extras = []
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--port" and i + 1 < len(args):
            porta = args[i + 1]
            i += 2
        elif args[i] == "--out" and i + 1 < len(args):
            pasta = Path(args[i + 1])
            i += 2
        elif args[i] == "--in-windows":
            i += 1
        else:
            extras.append(args[i])
            i += 1
    if "--in-windows" not in sys.argv:
        relancar_no_windows(porta, pasta, extras)

    pasta.mkdir(parents=True, exist_ok=True)
    print("serial %s -> %s" % (porta, pasta), flush=True)
    link = Serial(porta)
    tema = 3
    alcance = 30
    try:
        comando(link, "radar", 0.55)
        sonda = capturar(link, "sonda", 0)
        tema, ordem, raio, largura = estado_inicial(sonda)
        alcance = ordem[0]
        linhas = listar(link)
        nm_atual = alcance
        incerto = len(ordem) > 1 and raio is None and largura < 48
        if not linhas and raio is None and largura < 48:
            print("alcance curto sem avioes; radar em 30 NM", flush=True)
            linhas, nm_atual = ir_alcance(link, 30, 1)
            alcance = 30
            incerto = False
        quadro_radar(link, pasta, "radar.png", linhas, ())

        linhas, nm_atual = ir_alcance(link, 250, nm_atual)
        quadro_radar(link, pasta, "radar-250nm.png", linhas, ())

        for indice, nome in (
            (0, "config-wifi.png"),
            (1, "config-coordenadas.png"),
            (2, "config-filtros.png"),
            (3, "config-geral.png"),
            (4, "config-tema.png"),
        ):
            comando(link, "menu %d" % indice, 2.2 if indice == 0 else 0.9)
            im = capturar(link, nome, 0)
            salvar(im, pasta, nome)

        for indice, nome in enumerate(NOMES_TEMA):
            comando(link, "tema %d" % indice, 0.4)
            linhas, nm_atual = ir_alcance(link, 30, nm_atual)
            quadro_radar(link, pasta, "radar-tema-%s.png" % nome, linhas, CIAS[nome])

        if incerto:
            alcance = confirmar_alcance(link, sonda, tema, ordem)
        else:
            comando(link, "nm %d" % alcance, 0.4)
        comando(link, "tema %d" % tema, 0.4)
        comando(link, "solta", 0.2)
        comando(link, "radar", 0.5)
        print("restaurado: tema %s, %d NM" % (NOMES_TEMA[tema], alcance), flush=True)
    finally:
        link.close()


if __name__ == "__main__":
    main()
