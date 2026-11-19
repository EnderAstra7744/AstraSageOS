"""Tiny VT100/xterm-256 emulator used to check what the serial console shows.

Models what a real terminal does with the byte stream: cursor addressing, SGR colours
(16/256), erase with the current background (BCE), scrolling and xterm's deferred
wrap at the last column.
"""
import re

STD = [(0,0,0),(205,0,0),(0,205,0),(205,205,0),(0,0,238),(205,0,205),(0,205,205),(229,229,229),
       (127,127,127),(255,0,0),(0,255,0),(255,255,0),(92,92,255),(255,0,255),(0,255,255),(255,255,255)]

def xterm_rgb(n):
    if n is None: return None
    if n < 16: return STD[n]
    if n >= 232:
        g = 8 + 10 * (n - 232); return (g, g, g)
    n -= 16
    lv = [0, 95, 135, 175, 215, 255]
    return (lv[n // 36], lv[(n // 6) % 6], lv[n % 6])

class VT:
    def __init__(self, rows=25, cols=80):
        self.rows, self.cols = rows, cols
        self.reset()

    def reset(self):
        self.grid = [[(' ', None, None)] * self.cols for _ in range(self.rows)]
        self.x = self.y = 0
        self.pending = False
        self.fg = self.bg = None
        self.rev = False
        self.saved = (0, 0)
        self.cursor_visible = True

    # -- helpers
    def _blank(self):
        return (' ', self.fg, self.bg)

    def _scroll(self):
        self.grid.pop(0)
        self.grid.append([(' ', None, None)] * self.cols)

    def _lf(self):
        if self.y == self.rows - 1: self._scroll()
        else: self.y += 1

    def _put(self, ch):
        if self.pending:
            self.x = 0; self._lf(); self.pending = False
        fg, bg = (self.bg, self.fg) if self.rev else (self.fg, self.bg)
        self.grid[self.y][self.x] = (ch, fg, bg)
        if self.x == self.cols - 1: self.pending = True
        else: self.x += 1

    def _sgr(self, ps):
        if not ps: ps = [0]
        i = 0
        while i < len(ps):
            p = ps[i]
            if p == 0: self.fg = self.bg = None; self.rev = False
            elif p == 7: self.rev = True
            elif p == 27: self.rev = False
            elif 30 <= p <= 37: self.fg = p - 30
            elif 90 <= p <= 97: self.fg = p - 90 + 8
            elif 40 <= p <= 47: self.bg = p - 40
            elif 100 <= p <= 107: self.bg = p - 100 + 8
            elif 160 <= p <= 175: self.bg = p - 160                      # AstraSage extension: standard-palette background (neofetch)
            elif p == 39: self.fg = None
            elif p == 49: self.bg = None
            elif p in (38, 48) and i + 2 < len(ps) and ps[i + 1] == 5:
                if p == 38: self.fg = ps[i + 2]
                else: self.bg = ps[i + 2]
                i += 2
            i += 1

    def _csi(self, params, priv, final):
        ps = [int(p) if p else 0 for p in params.split(';')] if params else []
        def a(i, d):
            return ps[i] if i < len(ps) and ps[i] > 0 else d
        if priv:
            if final in 'hl' and params == '25': self.cursor_visible = (final == 'h')
            return
        if final == 'm': self._sgr(ps)
        elif final in 'Hf':
            self.y = min(a(0, 1) - 1, self.rows - 1); self.x = min(a(1, 1) - 1, self.cols - 1); self.pending = False
        elif final == 'A': self.y = max(0, self.y - a(0, 1))
        elif final == 'B': self.y = min(self.rows - 1, self.y + a(0, 1))
        elif final == 'C': self.x = min(self.cols - 1, self.x + a(0, 1)); self.pending = False
        elif final == 'D': self.x = max(0, self.x - a(0, 1)); self.pending = False
        elif final == 'G': self.x = min(self.cols - 1, a(0, 1) - 1); self.pending = False
        elif final == 'J':
            m = ps[0] if ps else 0
            if m == 0:
                for x in range(self.x, self.cols): self.grid[self.y][x] = self._blank()
                for y in range(self.y + 1, self.rows): self.grid[y] = [self._blank()] * self.cols
            elif m == 1:
                for y in range(0, self.y): self.grid[y] = [self._blank()] * self.cols
                for x in range(0, self.x + 1): self.grid[self.y][x] = self._blank()
            else:
                for y in range(self.rows): self.grid[y] = [self._blank()] * self.cols
        elif final == 'K':
            m = ps[0] if ps else 0
            rng = range(self.x, self.cols) if m == 0 else range(0, self.x + 1) if m == 1 else range(self.cols)
            for x in rng: self.grid[self.y][x] = self._blank()
        elif final == 's': self.saved = (self.x, self.y)
        elif final == 'u': self.x, self.y = self.saved; self.pending = False

    CSI = re.compile(rb'\x1b\[([?0-9;]*)([@-~])')

    def feed(self, data):
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b == 0x1b:
                m = self.CSI.match(data, i)
                if m:
                    params = m.group(1).decode()
                    priv = params.startswith('?')
                    self._csi(params.lstrip('?'), priv, chr(m.group(2)[0]))
                    i = m.end(); continue
                i += 1; continue
            i += 1
            if b == 0: continue
            if b == 0x0d: self.x = 0; self.pending = False
            elif b == 0x0a: self._lf()
            elif b == 0x08: self.x = max(0, self.x - 1); self.pending = False
            elif b >= 0x20:
                # decode utf-8 loosely: the OS only emits ASCII
                self._put(chr(b))

    # -- inspection
    def row_text(self, y):
        return ''.join(c[0] for c in self.grid[y]).rstrip()

    def text(self):
        return '\n'.join(self.row_text(y) for y in range(self.rows))

    def row_bg(self, y):
        return {c[2] for c in self.grid[y]}

    def render_png(self, path, scale=1, ttf='font/font.ttf'):
        from PIL import Image, ImageDraw, ImageFont
        cw, ch = 9, 18
        font = ImageFont.truetype(ttf, 16)
        img = Image.new('RGB', (self.cols * cw, self.rows * ch), (0, 0, 0))
        d = ImageDraw.Draw(img)
        for y in range(self.rows):
            for x in range(self.cols):
                c, fg, bg = self.grid[y][x]
                if bg is not None:
                    d.rectangle([x * cw, y * ch, x * cw + cw - 1, y * ch + ch - 1], fill=xterm_rgb(bg))
                if c != ' ':
                    d.text((x * cw, y * ch), c, font=font, fill=xterm_rgb(fg) if fg is not None else (200, 200, 200))
        if y == 0 and self.cursor_visible:
            pass
        if scale != 1: img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        img.save(path)
