#!/usr/bin/env python3
"""
Extracts all embedded BinaryData assets (SVGs, PNGs, TTF fonts, and localization tables)
directly from the local Windows PE binary FxSound/FxSound.exe.
"""

import hashlib
import json
import os
import re
import struct
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
PE_PATH = ROOT_DIR / "FxSound" / "FxSound.exe"
ASSETS_DIR = ROOT_DIR / "assets"
ANALYSIS_DIR = ROOT_DIR / "analysis"


SVG_NAME_MAP = {
    0x2CD8F0: "settings.svg",
    0x2CDE70: "maximize.svg",
    0x2CE1B0: "menu.svg",
    0x2CE3E0: "dropdown_arrow_bw.svg",
    0x2D0E70: "Slider_Thumb.svg",
    0x2DF1E0: "laughing.svg",
    0x2E1CF0: "Slider_Thumb_bw.svg",
    0x2E5450: "menu_hover.svg",
    0x2EFFB0: "minimize.svg",
    0x3072B0: "Button_OFF.svg",
    0x30B1D0: "Button_ON.svg",
    0x30BE80: "dropdown_arrow_hover.svg",
    0x31BEC0: "arrow_next.svg",
    0x31C590: "Bars.svg",
    0x31C7E0: "minimize_hover.svg",
    0x31D030: "arrow_prev.svg",
    0x31D230: "question.svg",
    0x31D720: "Logo.svg",
    0x3225F0: "maximize_hover.svg",
    0x33BF50: "arrow_next_bw.svg",
    0x33EAF0: "arrow_prev_bw.svg",
}

PNG_NAME_MAP = {
    0x2D09E0: "icons8-closed-eye-48.png",
    0x2D2190: "logo-red.png",
    0x2E56F0: "logo-white.png",
    0x30E9E0: "Bars.png",
    0x310310: "sonos-300x100.png",
    0x3131A0: "sonos-216x36.png",
    0x316730: "fxsound.png",
    0x31C0C0: "White.png",
    0x31CBB0: "icons8-eye-48.png",
    0x321DA0: "fxsound_large.png",
    0x4631C0: "app_icon_256_a.png",
    0x467530: "app_icon_256_b.png",
}

TTF_OFFSETS = [
    (0x2F0370, "Gilroy-Semibold-1.ttf"),
    (0x3250E0, "Gilroy-Semibold-2.ttf"),
    (0x343890, "Gilroy-Semibold-3.ttf"),
]

LANG_CODE_MAP = {
    "Chinese (Traditional)": "zh-TW",
    "Turkish": "tr",
    "Romanian": "ro",
    "Swedish": "sv",
    "French": "fr",
    "Russian": "ru",
    "Spanish": "es",
    "Portuguese": "pt",
    "Korean": "ko",
    "Vietnamese": "vi",
    "German": "de",
    "Italiano": "it",
    "Indonesian": "id",
    "Polish": "pl",
    "Chinese (Simplified)": "zh-CN",
    "Brazilian Portuguese": "pt-BR",
}


def sha256_hex(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


def parse_lang_translations(raw_text: str) -> dict:
    """Parses JUCE LocalisedStrings format: "English Key" = "Translated Value" """
    entries = {}
    pattern = re.compile(r'"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"')
    for m in pattern.finditer(raw_text):
        k = m.group(1).replace('\\"', '"').replace("\\r\\n", "\n").replace("\\n", "\n").replace("\\'", "'")
        v = m.group(2).replace('\\"', '"').replace("\\r\\n", "\n").replace("\\n", "\n").replace("\\'", "'")
        entries[k] = v
    return entries


def main() -> None:
    if not PE_PATH.exists():
        raise FileNotFoundError(f"Missing binary: {PE_PATH}")

    data = PE_PATH.read_bytes()

    svg_dir = ASSETS_DIR / "svg"
    png_dir = ASSETS_DIR / "png"
    font_dir = ASSETS_DIR / "fonts"
    i18n_dir = ASSETS_DIR / "i18n"
    for d in (svg_dir, png_dir, font_dir, i18n_dir, ANALYSIS_DIR):
        d.mkdir(parents=True, exist_ok=True)

    manifest = {
        "source_binary": str(PE_PATH.relative_to(ROOT_DIR)),
        "source_sha256": sha256_hex(data),
        "svgs": [],
        "pngs": [],
        "fonts": [],
        "locales": [],
    }

    # 1. Extract all 21 SVGs
    for m in re.finditer(
        rb"(?:<\?xml[^>]*>\s*)?<svg[^>]*>.*?</svg>",
        data[0x2CD000:0x358000],
        re.DOTALL,
    ):
        off = 0x2CD000 + m.start()
        end = 0x2CD000 + m.end()
        blob = m.group(0)
        name = SVG_NAME_MAP.get(off, f"svg_{off:06x}.svg")
        out_path = svg_dir / name
        out_path.write_bytes(blob)
        manifest["svgs"].append(
            {
                "name": name,
                "offset": f"0x{off:06x}",
                "end": f"0x{end:06x}",
                "bytes": len(blob),
                "sha256": sha256_hex(blob),
            }
        )

    # 2. Extract all 12 PNGs
    for m in re.finditer(rb"\x89PNG\r\n\x1a\n.*?IEND\xaeB`\x82", data, re.DOTALL):
        off = m.start()
        end = m.end()
        blob = m.group(0)
        w, h = struct.unpack(">II", blob[16:24])
        name = PNG_NAME_MAP.get(off, f"png_{off:06x}_{w}x{h}.png")
        out_path = png_dir / name
        out_path.write_bytes(blob)
        manifest["pngs"].append(
            {
                "name": name,
                "offset": f"0x{off:06x}",
                "end": f"0x{end:06x}",
                "width": w,
                "height": h,
                "bytes": len(blob),
                "sha256": sha256_hex(blob),
            }
        )

    # 3. Extract embedded TTF fonts
    for off, name in TTF_OFFSETS:
        num_tables = struct.unpack(">H", data[off + 4 : off + 6])[0]
        max_end = 0
        for i in range(num_tables):
            t_off, t_len = struct.unpack(
                ">II", data[off + 12 + i * 16 + 8 : off + 12 + i * 16 + 16]
            )
            max_end = max(max_end, t_off + t_len)
        blob = data[off : off + max_end]
        out_path = font_dir / name
        out_path.write_bytes(blob)
        manifest["fonts"].append(
            {
                "name": name,
                "offset": f"0x{off:06x}",
                "bytes": len(blob),
                "sha256": sha256_hex(blob),
            }
        )

    # 4. Extract all 16 embedded localization tables + English keys
    all_translations = {"en": {}}
    for m in re.finditer(rb"language:\s*([^\r\n]+)", data[0x2CD000:0x344000]):
        off = 0x2CD000 + m.start()
        end = data.find(b"\x00", off)
        raw_bytes = data[off:end]
        raw_text = raw_bytes.decode("utf-8", "replace")
        lang_name = m.group(1).decode("utf-8", "replace").strip()
        lang_code = LANG_CODE_MAP.get(lang_name, lang_name.lower().replace(" ", "_"))
        out_txt = i18n_dir / f"FxSound.{lang_code}.txt"
        out_txt.write_bytes(raw_bytes)
        parsed = parse_lang_translations(raw_text)
        all_translations[lang_code] = {
            "language_name": lang_name,
            "strings": parsed,
        }
        for k in parsed:
            all_translations["en"][k] = k
        manifest["locales"].append(
            {
                "code": lang_code,
                "language_name": lang_name,
                "offset": f"0x{off:06x}",
                "bytes": len(raw_bytes),
                "string_count": len(parsed),
            }
        )

    all_translations["en"] = {
        "language_name": "English",
        "strings": all_translations["en"],
    }
    (i18n_dir / "translations.json").write_text(
        json.dumps(all_translations, indent=2, ensure_ascii=False), encoding="utf-8"
    )

    manifest_path = ANALYSIS_DIR / "extracted-assets.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        f"Extracted {len(manifest['svgs'])} SVGs, {len(manifest['pngs'])} PNGs, "
        f"{len(manifest['fonts'])} TTF fonts, and {len(manifest['locales'])} locale tables."
    )


if __name__ == "__main__":
    main()
