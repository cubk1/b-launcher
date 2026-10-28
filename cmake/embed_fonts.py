import io
import os
import re
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

TEXT_FONT = ("textFont", "assets/fonts/SSTRg.TTF")
ICON_FONT = ("iconFont", "assets/fonts/fa-solid-900.ttf")
EXTRA_ICONS = []
ICON_FOLDERS = ["src/app", "src/ui"]
PER_LINE = 32
ICON_LITERAL = re.compile(r"\b0[xX]([eEfF][0-9a-fA-F]{3})\b")
UNUSED_TABLES = ["kern", "GPOS", "GSUB", "GDEF", "DSIG", "hdmx", "LTSH", "VDMX", "PCLT", "FFTM"]


def used_icons(root):
    found = set(EXTRA_ICONS)
    for top in ICON_FOLDERS:
        for folder, _, names in os.walk(os.path.join(root, top)):
            for name in names:
                if not name.endswith((".cpp", ".h")):
                    continue
                with open(os.path.join(folder, name), encoding="utf-8", errors="ignore") as handle:
                    for match in ICON_LITERAL.findall(handle.read()):
                        found.add(int(match, 16))
    return sorted(code for code in found if 0xE000 <= code <= 0xF8FF)


def cut(path, unicodes):
    options = subset.Options()
    options.hinting = False
    options.drop_tables += UNUSED_TABLES
    options.layout_features = []
    options.glyph_names = False
    options.notdef_outline = True
    font = TTFont(path)
    if unicodes is None:
        unicodes = sorted(font.getBestCmap().keys())
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=unicodes)
    subsetter.subset(font)
    out = io.BytesIO()
    font.save(out)
    return out.getvalue()


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output = sys.argv[1]
    fonts = [
        (TEXT_FONT[0], cut(os.path.join(root, TEXT_FONT[1]), None)),
        (ICON_FONT[0], cut(os.path.join(root, ICON_FONT[1]), used_icons(root))),
    ]
    lines = ['#include "EmbeddedFonts.h"', "", "namespace EmbeddedFonts {", ""]
    for symbol, data in fonts:
        lines.append("    alignas(4) const unsigned char %s[%d] = {" % (symbol, len(data)))
        for start in range(0, len(data), PER_LINE):
            lines.append("        " + ",".join(str(b) for b in data[start:start + PER_LINE]) + ",")
        lines.append("    };")
        lines.append("    const unsigned int %sSize = %d;" % (symbol, len(data)))
        lines.append("")
    lines.append("}")
    text = "\n".join(lines) + "\n"
    unchanged = False
    if os.path.exists(output):
        with open(output, encoding="utf-8") as handle:
            unchanged = handle.read() == text
    if not unchanged:
        os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
        with open(output, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        print("wrote", output)
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w") as handle:
            handle.write("")


if __name__ == "__main__":
    main()
