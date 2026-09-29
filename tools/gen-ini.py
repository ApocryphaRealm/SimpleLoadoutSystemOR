"""Write the shipped INI from the compiled defaults (rule 16: the DLL's defaults and the shipped INI must agree).
The text is the DefaultIniText() in src/Settings.cpp, kept here as the same lines.

    python tools/gen-ini.py           write dist/.../SimpleLoadoutSystem.ini
    python tools/gen-ini.py --check   exit 1 if the file on disk differs
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INI = os.path.join(ROOT, "dist", "OblivionRemastered", "Binaries", "Win64", "OBSE", "Plugins", "SimpleLoadoutSystem.ini")
SRC = os.path.join(ROOT, "src", "Settings.cpp")


def text_from_source():
    src = io.open(SRC, encoding="utf-8").read()
    body = src[src.index("std::string DefaultIniText()"):]
    body = body[:body.index("return s;")]
    pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    out = "".join(p.encode("utf-8").decode("unicode_escape") for p in pieces if "sLoadoutName\" + std" not in p)
    # the loop in the source writes sLoadoutName1..10 between the two literal blocks
    head, tail = out.split("\n[Debug]")
    names = "".join("sLoadoutName%d=\n" % i for i in range(1, 11))
    out = head + names + "\n[Debug]" + tail
    return out.replace("\n", "\r\n")


def main():
    want = text_from_source().encode("utf-8")
    if "--check" in sys.argv:
        have = io.open(INI, "rb").read() if os.path.exists(INI) else b""
        if have != want:
            sys.exit("gen-ini: %s differs from the compiled defaults" % INI)
        print("gen-ini: OK")
        return
    os.makedirs(os.path.dirname(INI), exist_ok=True)
    io.open(INI, "wb").write(want)
    print("gen-ini: wrote", INI)


if __name__ == "__main__":
    main()
