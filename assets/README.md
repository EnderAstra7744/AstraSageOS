# AstraSageOS assets

The kernel embeds three BMP files (gui/assets.S) - uncompressed 24-bit, which is what gui/bmp.c decodes:

| file | used for |
|------|----------|
| `boot_logo.bmp` | boot splash only |
| `desktop_wallpaper.bmp` | desktop background only (scaled to the screen, "cover") |
| `ast_icon.bmp` | the AST application icon (dock and desktop, through `draw_app_icon`) |

`src/*.jpg` are the original images you supplied; the BMPs were made from them with
`python3 assets/make_assets.py` (Pillow). Replace a BMP with your own 24/32-bit uncompressed BMP and
rebuild; a missing or unsupported image falls back to a plain look and prints a notice in AST.
