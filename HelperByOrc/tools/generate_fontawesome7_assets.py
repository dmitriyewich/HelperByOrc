#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LUA = Path(r"C:\Games\CODEX\HelperByOrc\lib\fAwesome7.lua")
DEFAULT_FA_VENDOR = REPO_ROOT / "external" / "fontawesome-free-7.2.0"
IMGUI_COMPRESSOR_CPP = REPO_ROOT / "external" / "imgui" / "misc" / "fonts" / "binary_to_compressed_c.cpp"
VSDEVCMD = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat")


@dataclass(frozen=True)
class Icon:
    id: str
    style: str
    glyph: str
    codepoint: int
    category: str


CATEGORY_ENUM_NAMES = {
    "general": "General",
    "brands": "Brands",
    "transport": "Transport",
    "game": "Game",
    "documents": "Documents",
    "communication": "Communication",
    "status": "Status",
    "money": "Money",
    "settings": "Settings",
    "people": "People",
}


def canonical_id(name: str) -> str:
    value = name.strip().lower().replace("_", "-")
    value = re.sub(r"[^a-z0-9-]+", "-", value)
    value = re.sub(r"-+", "-", value).strip("-")
    return value


def encode_utf8_cpp(value: str) -> str:
    return "".join(f"\\x{byte:02X}" for byte in value.encode("utf-8"))


def cpp_string(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
        .replace("\t", "\\t")
    )


def lua_bytes_to_text(value: str) -> str:
    data = bytes(int(byte, 16) for byte in re.findall(r"\\x([0-9A-Fa-f]{2})", value))
    return data.decode("utf-8")


def parse_lua_icons(lua_path: Path) -> tuple[list[tuple[str, str, int]], str]:
    if not lua_path.exists():
        raise FileNotFoundError(f"Lua Font Awesome source is missing: {lua_path}")

    text = lua_path.read_text(encoding="utf-8")
    blob_match = re.search(r'local\s+fa_solid_compressed_data_base85\s*=\s*"([^"]+)"', text)
    if not blob_match:
        raise RuntimeError("fa_solid_compressed_data_base85 was not found in Lua source")

    entries: list[tuple[str, str, int]] = []
    for raw_name, escaped in re.findall(r'\["([^"\]]+)"\]\s*=\s*"((?:\\x[0-9A-Fa-f]{2})+)"', text):
        glyph = lua_bytes_to_text(escaped)
        if len(glyph) != 1:
            continue
        codepoint = ord(glyph)
        if not (0xE000 <= codepoint <= 0xF8FF):
            continue
        entries.append((canonical_id(raw_name), glyph, codepoint))

    if not entries:
        raise RuntimeError("No valid solid private-use glyphs were parsed from Lua source")

    entries.sort(key=lambda item: item[0])
    return entries, blob_match.group(1)


def validate_brands_source(vendor_dir: Path) -> tuple[Path, Path]:
    metadata_path = vendor_dir / "metadata" / "icons.json"
    font_path = vendor_dir / "otfs" / "Font Awesome 7 Brands-Regular-400.otf"
    license_path = vendor_dir / "LICENSE.txt"

    missing = [str(path) for path in (metadata_path, font_path, license_path) if not path.exists()]
    if missing:
        raise FileNotFoundError("Font Awesome Free Brands vendored source is incomplete: " + ", ".join(missing))

    signature = font_path.read_bytes()[:4]
    if signature != b"OTTO":
        raise RuntimeError(f"Brands font must be the official OpenType/CFF OTF, got signature {signature!r}")

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    discord = metadata.get("discord", {})
    if "brands" not in discord.get("free", []) and "brands" not in discord.get("styles", []):
        raise RuntimeError("Brands metadata validation failed: discord brand is missing")

    return metadata_path, font_path


def compile_compressor(temp_dir: Path) -> Path:
    exe_path = temp_dir / "binary_to_compressed_c.exe"
    if not IMGUI_COMPRESSOR_CPP.exists():
        raise FileNotFoundError(f"ImGui compressor source is missing: {IMGUI_COMPRESSOR_CPP}")

    cl = shutil.which("cl.exe")
    if cl:
        cmd = [cl, "/nologo", "/O2", "/EHsc", str(IMGUI_COMPRESSOR_CPP), f"/Fe:{exe_path}"]
        subprocess.run(cmd, cwd=temp_dir, check=True)
        return exe_path

    if VSDEVCMD.exists():
        build_bat = temp_dir / "build_compressor.bat"
        build_bat.write_text(
            "\n".join(
                [
                    "@echo off",
                    f'call "{VSDEVCMD}" -arch=x64 -host_arch=x64 >nul',
                    "if errorlevel 1 exit /b 1",
                    f'cl /nologo /O2 /EHsc "{IMGUI_COMPRESSOR_CPP}" /Fe:"{exe_path}"',
                    "if errorlevel 1 exit /b 1",
                ]
            ),
            encoding="utf-8",
            newline="\r\n",
        )
        subprocess.run(["cmd", "/d", "/c", str(build_bat)], cwd=temp_dir, check=True)
        return exe_path

    raise RuntimeError("cl.exe is not in PATH and VsDevCmd.bat was not found; cannot build ImGui font compressor")


def base85_from_font(font_path: Path) -> str:
    with tempfile.TemporaryDirectory(prefix="hbo_fa_") as tmp:
        temp_dir = Path(tmp)
        exe = compile_compressor(temp_dir)
        output = subprocess.check_output([str(exe), "-base85", str(font_path), "BrandsFont"], cwd=temp_dir)
    text = output.decode("utf-8", errors="strict")
    marker = "BrandsFont_compressed_data_base85"
    marker_pos = text.find(marker)
    if marker_pos < 0:
        raise RuntimeError("Compressed Base85 symbol was not found in ImGui compressor output")
    strings = re.findall(r'"([^"]*)"', text[marker_pos:])
    if not strings:
        raise RuntimeError("Failed to parse Base85 output from ImGui compressor")
    return "".join(strings).replace("\\?", "?")


def decode85_byte(ch: str) -> int:
    value = ord(ch)
    return value - 36 if value >= ord("\\") else value - 35


def decode_base85(blob: str) -> bytes:
    if len(blob) % 5 != 0:
        raise RuntimeError(f"Base85 blob length must be divisible by 5, got {len(blob)}")

    out = bytearray()
    for index in range(0, len(blob), 5):
        chunk = blob[index : index + 5]
        value = (
            decode85_byte(chunk[0])
            + 85
            * (
                decode85_byte(chunk[1])
                + 85
                * (
                    decode85_byte(chunk[2])
                    + 85 * (decode85_byte(chunk[3]) + 85 * decode85_byte(chunk[4]))
                )
            )
        )
        out.extend(value.to_bytes(4, "little"))
    return bytes(out)


def classify_icon(icon_id: str, style: str) -> str:
    if style == "brands":
        return "brands"
    tokens = set(icon_id.split("-"))
    joined = icon_id
    if tokens & {"car", "cars", "truck", "taxi", "bus", "motorcycle", "vehicle", "steering"}:
        return "transport"
    if tokens & {"gun", "sword", "swords", "knife", "shield", "bomb"}:
        return "game"
    if tokens & {"folder", "file", "book", "note", "clipboard"}:
        return "documents"
    if tokens & {"message", "messages", "comment", "comments", "chat"}:
        return "communication"
    if tokens & {"heart", "shield", "kit", "medical", "hospital"}:
        return "status"
    if tokens & {"money", "dollar", "bank", "landmark", "coins", "wallet"}:
        return "money"
    if tokens & {"gear", "gears", "sliders", "wrench", "screwdriver"}:
        return "settings"
    if tokens & {"user", "users", "people", "person", "id", "address"}:
        return "people"
    if "arizona" in joined:
        return "game"
    return "general"


def parse_brands_icons(metadata_path: Path) -> list[tuple[str, str, int]]:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    icons: list[tuple[str, str, int]] = []
    for raw_id, data in metadata.items():
        if "brands" not in data.get("free", []) and "brands" not in data.get("styles", []):
            continue
        icon_id = canonical_id(raw_id)
        unicode_value = data.get("unicode")
        if not isinstance(unicode_value, str):
            continue
        codepoint = int(unicode_value, 16)
        if not (0xE000 <= codepoint <= 0xF8FF):
            continue
        icons.append((icon_id, chr(codepoint), codepoint))

    if not icons:
        raise RuntimeError("No valid brands private-use glyphs were parsed from metadata")
    icons.sort(key=lambda item: item[0])
    return icons


def coalesced_ranges(codepoints: list[int]) -> list[tuple[int, int]]:
    values = sorted(set(codepoints))
    if not values:
        return []

    ranges: list[tuple[int, int]] = []
    start = prev = values[0]
    for cp in values[1:]:
        if cp == prev + 1:
            prev = cp
            continue
        ranges.append((start, prev))
        start = prev = cp
    ranges.append((start, prev))
    return ranges


def build_icons(lua_path: Path, vendor_dir: Path) -> tuple[list[Icon], bytes, bytes, list[tuple[int, int]], list[tuple[int, int]]]:
    solid_entries, solid_blob = parse_lua_icons(lua_path)
    metadata_path, brands_font = validate_brands_source(vendor_dir)
    brands_entries = parse_brands_icons(metadata_path)
    brands_blob = base85_from_font(brands_font)

    icons: list[Icon] = []
    for icon_id, glyph, codepoint in solid_entries:
        category = classify_icon(icon_id, "solid")
        icons.append(Icon(icon_id, "solid", glyph, codepoint, category))

    for icon_id, glyph, codepoint in brands_entries:
        category = classify_icon(icon_id, "brands")
        icons.append(Icon(icon_id, "brands", glyph, codepoint, category))

    icons.sort(key=lambda icon: (0 if icon.style == "solid" else 1, icon.id))
    solid_ranges = coalesced_ranges([icon.codepoint for icon in icons if icon.style == "solid"])
    brands_ranges = coalesced_ranges([icon.codepoint for icon in icons if icon.style == "brands"])
    return icons, decode_base85(solid_blob), decode_base85(brands_blob), solid_ranges, brands_ranges


def write_font_data(path: Path, solid_data: bytes, brands_data: bytes) -> None:
    def u32_words(data: bytes) -> list[int]:
        if len(data) % 4 != 0:
            data += b"\0" * (4 - (len(data) % 4))
        return [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]

    def append_u32_array(lines: list[str], symbol: str, data: bytes) -> None:
        words = u32_words(data)
        lines.append(f"inline constexpr unsigned int {symbol}Size = {len(data)};")
        lines.append(f"alignas(4) inline constexpr unsigned int {symbol}Data[] = {{")
        for index in range(0, len(words), 8):
            chunk = words[index : index + 8]
            lines.append("    " + ", ".join(f"0x{word:08X}u" for word in chunk) + ",")
        lines.append("};")

    lines = [
        "#pragma once",
        "",
        "// Generated by tools/generate_fontawesome7_assets.py. Do not edit manually.",
        "namespace FontAwesome7Data {",
    ]
    append_u32_array(lines, "kSolidCompressed", solid_data)
    lines.append("")
    append_u32_array(lines, "kBrandsCompressed", brands_data)
    lines.extend(["", "} // namespace FontAwesome7Data", ""])
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_registry_data(path: Path, icons: list[Icon], solid_ranges: list[tuple[int, int]], brands_ranges: list[tuple[int, int]]) -> None:
    solid_count = sum(1 for icon in icons if icon.style == "solid")
    brands_count = len(icons) - solid_count
    lines = [
        "#pragma once",
        "",
        "// Generated by tools/generate_fontawesome7_assets.py. Do not edit manually.",
        "#include \"icon_registry.h\"",
        "",
        "namespace icon_registry::generated {",
        "",
        f"inline constexpr std::size_t kSolidIconCount = {solid_count};",
        f"inline constexpr std::size_t kBrandIconCount = {brands_count};",
        "",
        "inline constexpr IconEntry kIcons[] = {",
    ]
    for icon in icons:
        style = "IconStyle::Solid" if icon.style == "solid" else "IconStyle::Brands"
        category = CATEGORY_ENUM_NAMES.get(icon.category, "General")
        glyph = encode_utf8_cpp(icon.glyph)
        lines.append(
            f'    {{ "{cpp_string(icon.id)}", {style}, "{glyph}", 0x{icon.codepoint:04X}, '
            f"IconCategory::{category} }},"
        )
    lines.extend(["};", "", "inline constexpr ImWchar kSolidRanges[] = {"])
    for start, end in solid_ranges:
        lines.append(f"    0x{start:04X}, 0x{end:04X},")
    lines.extend(["    0,", "};", "", "inline constexpr ImWchar kBrandsRanges[] = {"])
    for start, end in brands_ranges:
        lines.append(f"    0x{start:04X}, 0x{end:04X},")
    lines.extend(["    0,", "};", "", "} // namespace icon_registry::generated", ""])
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate full Font Awesome 7 assets for HelperByOrc.")
    parser.add_argument("--lua", type=Path, default=Path(os.environ.get("HELPERBYORC_FA_LUA", DEFAULT_LUA)))
    parser.add_argument("--vendor", type=Path, default=Path(os.environ.get("HELPERBYORC_FA_VENDOR", DEFAULT_FA_VENDOR)))
    parser.add_argument("--font-data", type=Path, default=REPO_ROOT / "font_awesome7_data.h")
    parser.add_argument("--registry-data", type=Path, default=REPO_ROOT / "icon_registry_data.h")
    args = parser.parse_args()

    icons, solid_blob, brands_blob, solid_ranges, brands_ranges = build_icons(args.lua, args.vendor)
    write_font_data(args.font_data, solid_blob, brands_blob)
    write_registry_data(args.registry_data, icons, solid_ranges, brands_ranges)

    print(f"solid icons: {sum(1 for icon in icons if icon.style == 'solid')}")
    print(f"brand icons: {sum(1 for icon in icons if icon.style == 'brands')}")
    print(f"solid ranges: {len(solid_ranges)}")
    print(f"brand ranges: {len(brands_ranges)}")
    print(f"generated: {args.font_data}")
    print(f"generated: {args.registry_data}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
