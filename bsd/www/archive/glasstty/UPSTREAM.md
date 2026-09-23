# Glass TTY VT220

Author: Viacheslav Slavinsky.
Upstream: https://github.com/svofski/glasstty
Revision: `2bed112c6844db70fbd857ab138d3fc283439102`.

The upstream font is released under the Unlicense (public-domain dedication).
The unchanged upstream `LICENSE` and `Glass_TTY_VT220.ttf` are preserved here.
The license permits use, modification and redistribution, including commercial use.

Original TTF SHA-256:
`cc4d515abd736808f429f2df2fe950b6c13468c7a889a7032d074aa05f246958`.

`../../site/glass-tty-vt220-v1.woff2` is a WOFF2 compression of that font, with
all glyphs retained. It is 6,536 bytes, generated with FontTools 4.66.0 and
Brotli 1.2.0. No glyph outlines or metrics were edited.

To regenerate on a modern host with those packages installed:

```python
from fontTools.ttLib import TTFont
font = TTFont('archive/glasstty/Glass_TTY_VT220.ttf')
font.flavor = 'woff2'
font.save('site/glass-tty-vt220-v1.woff2')
```

The Gary page loads the font from the same origin, with a monospace fallback.
Its CRT area uses 20px body text and the font's built-in scanline appearance;
the previous CSS scanline overlay is removed to avoid doubling the effect.
Cabinet lettering retains the existing font. Future font changes should use
a new public asset filename so cached copies remain compatible.
