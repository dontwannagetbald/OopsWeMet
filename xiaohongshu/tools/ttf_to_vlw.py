#!/usr/bin/env python3
"""Convert a TTF/OTF font into an M5GFX/LovyanGFX VLW bitmap font."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def gb2312_chars() -> set[str]:
    chars: set[str] = set()
    for high in range(0xB0, 0xF8):
        for low in range(0xA1, 0xFF):
            try:
                chars.add(bytes((high, low)).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return chars


def default_charset(extra: str = "") -> list[str]:
    chars = set(chr(code) for code in range(0x21, 0x7F))
    chars.update(gb2312_chars())
    chars.update(extra)
    chars.discard(" ")
    return sorted((ch for ch in chars if ord(ch) <= 0xFFFF), key=ord)


def render_glyph(font: ImageFont.FreeTypeFont, ch: str) -> tuple[list[int], bytes]:
    bbox = font.getbbox(ch, anchor="ls")
    x0, y0, x1, y1 = bbox
    width = max(0, x1 - x0)
    height = max(0, y1 - y0)
    advance = max(1, int(round(font.getlength(ch))))
    dy = max(0, -y0)
    dx = x0

    if width == 0 or height == 0:
        return [ord(ch), 0, 0, advance, dy, dx, 0], b""

    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    draw.text((-x0, -y0), ch, font=font, fill=255, anchor="ls")

    return [ord(ch), height, width, advance, dy, dx, 0], image.tobytes()


def write_vlw(font_path: Path, output_path: Path, size: int, chars: list[str]) -> None:
    font = ImageFont.truetype(str(font_path), size=size)
    ascent, descent = font.getmetrics()

    records: list[list[int]] = []
    bitmaps: list[bytes] = []

    for ch in chars:
        try:
            record, bitmap = render_glyph(font, ch)
        except Exception:
            continue
        if record[2] > 255 or record[3] > 255:
            continue
        records.append(record)
        bitmaps.append(bitmap)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as f:
        f.write(struct.pack(">6I", len(records), 11, size, 0, ascent, descent))
        for record in records:
            f.write(struct.pack(">IIIIiii", *record))
        for bitmap in bitmaps:
            f.write(bitmap)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("font", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--size", type=int, default=24)
    parser.add_argument("--chars-file", type=Path)
    parser.add_argument("--extra", default="张总遇见小红谈话结束准备回家。小夏你好收到切换正常硬件测试开始，：！？…")
    args = parser.parse_args()

    if args.chars_file:
        chars = sorted(set(args.chars_file.read_text(encoding="utf-8")) - {" "}, key=ord)
    else:
        chars = default_charset(args.extra)

    write_vlw(args.font, args.output, args.size, chars)


if __name__ == "__main__":
    main()
