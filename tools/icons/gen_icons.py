
from PIL import Image, ImageDraw
import os

# grava direto em icons/ (antes ia para icons/_new e exigia copia manual;
# a barra carrega os PNG em runtime, entao _new ficava velho)
OUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "icons"))
os.makedirs(OUT, exist_ok=True)
S = 4           # supersampling
SZ = 16 * S     # 64
W = 4           # ~1.0px no final (VS medido)
C = (255, 255, 255, 255)

def canvas():
    im = Image.new("RGBA", (SZ, SZ), (0,0,0,0))
    return im, ImageDraw.Draw(im)

def P(x, y):    # coordenadas 0..16 -> 0..64
    return (x*S, y*S)

def line(d, a, b, w=W):
    d.line([P(*a), P(*b)], fill=C, width=w)

def poly(d, pts, w=W):
    d.line([P(*p) for p in pts], fill=C, width=w, joint="curve")

def rect(d, x0, y0, x1, y1, w=W, r=0):
    d.rounded_rectangle([P(x0,y0), P(x1,y1)], radius=r*S, outline=C, width=w)

def circle(d, cx, cy, rad, w=W):
    d.ellipse([P(cx-rad, cy-rad), P(cx+rad, cy+rad)], outline=C, width=w)

def save(im, name):
    im.resize((16,16), Image.LANCZOS).save(os.path.join(OUT, name + ".png"))

# ---------------- glifos ----------------
def acquisition():
    im, d = canvas()
    line(d, (8,2), (8,10)); poly(d, [(5,7),(8,10.5),(11,7)])
    poly(d, [(3,11.5),(3,13.5),(13,13.5),(13,11.5)])
    return im, "acquisition"

def artifacts():
    im, d = canvas()
    poly(d, [(8,2),(13.5,4.7),(13.5,11.3),(8,14),(2.5,11.3),(2.5,4.7),(8,2)])
    poly(d, [(2.5,4.7),(8,7.4),(13.5,4.7)]); line(d, (8,7.4), (8,14))
    return im, "artifacts"

def e01():
    im, d = canvas()
    poly(d, [(4,2),(10,2),(12.5,4.5),(12.5,14),(4,14),(4,2)])
    line(d, (6,6), (10.5,6)); line(d, (6,8.5), (10.5,8.5)); line(d, (6,11), (8.5,11))
    return im, "e01"

def pericia():
    im, d = canvas()
    rect(d, 2.5, 2.5, 10, 13, r=1)
    line(d, (5,6), (8,6)); line(d, (5,8.5), (7,8.5))
    circle(d, 10.5, 10.5, 3); line(d, (12.8,12.8), (14.5,14.5))
    return im, "pericia"

def recovery():
    im, d = canvas()
    d.arc([P(3,3), P(13,13)], start=40, end=320, fill=C, width=W)
    poly(d, [(11.5,2.5),(13.5,4.2),(10.8,5.6)])
    return im, "recovery"

def sanitizacao():
    im, d = canvas()
    poly(d, [(8,2),(13.5,4),(13.5,8.5),(8,14),(2.5,8.5),(2.5,4),(8,2)])
    poly(d, [(5.5,8),(7.3,9.8),(10.5,6.3)])
    return im, "sanitizacao"

def wipe():
    im, d = canvas()
    line(d, (3,4.5), (13,4.5)); line(d, (6.5,4.5), (6.5,2.5)); line(d, (9.5,4.5), (9.5,2.5))
    line(d, (6.5,2.5), (9.5,2.5))
    poly(d, [(4.5,4.5),(5.5,13.8),(10.5,13.8),(11.5,4.5)])
    line(d, (7,7), (7,11.5)); line(d, (9,7), (9,11.5))
    return im, "wipe"

def kernel():
    im, d = canvas()
    rect(d, 4.5, 4.5, 11.5, 11.5, r=1)
    rect(d, 6.8, 6.8, 9.2, 9.2, r=0.4)
    for t in (6, 8, 10):
        line(d, (t,1.5), (t,4.5)); line(d, (t,11.5), (t,14.5))
        line(d, (1.5,t), (4.5,t)); line(d, (11.5,t), (14.5,t))
    return im, "kernel"

def hardware():
    im, d = canvas()
    rect(d, 1.8, 3, 12.8, 10.2, r=1)
    line(d, (6.5,13.5), (10,13.5)); line(d, (8,10.2), (8,13.5))
    return im, "hardware"

def cloud():
    im, d = canvas()
    d.arc([P(2.2,5.2), P(8.4,11.4)], start=150, end=10, fill=C, width=W)
    d.arc([P(5.6,3.6), P(11.8,9.8)], start=190, end=40, fill=C, width=W)
    d.arc([P(7.6,5.8), P(14.2,12.4)], start=200, end=340, fill=C, width=W)
    line(d, (3.2,10.8), (12.4,10.8))
    return im, "cloud"

def refresh():
    im, d = canvas()
    d.arc([P(3,3), P(13,13)], start=30, end=330, fill=C, width=W)
    poly(d, [(10.2,2.4),(13.2,3.2),(12.2,6.2)])
    return im, "refresh"

def search():
    im, d = canvas()
    circle(d, 7, 7, 4.4); line(d, (10.3,10.3), (14,14))
    return im, "search"

def terminal():
    im, d = canvas()
    rect(d, 1.8, 3, 14.2, 13, r=1)
    poly(d, [(4.6,7),(6.6,9),(4.6,11)])
    line(d, (8.6,11), (11.6,11))
    return im, "terminal"

def tab_table():
    im, d = canvas()
    rect(d, 2, 3, 14, 13, r=1)
    line(d, (2,6), (14,6)); line(d, (7.2,6), (7.2,13)); line(d, (2,9.5), (14,9.5))
    return im, "tab_table"

def tab_hex():
    im, d = canvas()
    poly(d, [(8,1.8),(13.6,4.9),(13.6,11.1),(8,14.2),(2.4,11.1),(2.4,4.9),(8,1.8)])
    line(d, (6.2,6.2), (6.2,10)); line(d, (9.8,6.2), (9.8,10))
    return im, "tab_hex"

def tab_timeline():
    im, d = canvas()
    line(d, (1.8,8), (14.2,8))
    poly(d, [(2.6,8),(4.2,4.4),(5.8,11.6),(7.4,6.2),(9,9.8),(10.6,5.2),(12.2,8),(13.4,8)])
    return im, "tab_timeline"

def expand_all():
    im, d = canvas()
    poly(d, [(4,9),(8,5),(12,9)]); poly(d, [(4,14),(8,10),(12,14)])
    return im, "expand_all"

def collapse_all():
    im, d = canvas()
    poly(d, [(4,5),(8,9),(12,5)]); poly(d, [(4,10),(8,14),(12,10)])
    return im, "collapse_all"

def folder():
    im, d = canvas()
    poly(d, [(2,4.5),(6.5,4.5),(8,6.2),(14,6.2),(14,13),(2,13),(2,4.5)])
    return im, "folder"

def file():
    im, d = canvas()
    poly(d, [(4,1.8),(10,1.8),(12.5,4.3),(12.5,14.2),(4,14.2),(4,1.8)])
    line(d, (6,7), (10,7)); line(d, (6,9.5), (10,9.5))
    return im, "file"

def action_read():
    im, d = canvas()
    poly(d, [(3,2.5),(11,2.5),(13,4.5),(13,13.5),(3,13.5),(3,2.5)])
    line(d, (5.2,6), (10.8,6)); line(d, (5.2,8.5), (10.8,8.5)); line(d, (5.2,11), (8.5,11))
    return im, "action_read"

def action_help():
    im, d = canvas()
    circle(d, 8, 8, 6)
    d.arc([P(5.8,5.2), P(10.2,9.2)], start=200, end=20, fill=C, width=W)
    line(d, (8,9.4), (8,10.8))
    d.ellipse([P(7.5,12.2), P(8.5,13.2)], fill=C)
    return im, "action_help"

made = []
for fn in (acquisition, artifacts, e01, pericia, recovery, sanitizacao, wipe, kernel, hardware, cloud,
           refresh, search, terminal, tab_table, tab_hex, tab_timeline, expand_all, collapse_all,
           folder, file, action_read, action_help):
    im, name = fn()
    save(im, name)
    made.append(name)
print("gerados:", len(made), "->", ", ".join(made[:8]), "...")
