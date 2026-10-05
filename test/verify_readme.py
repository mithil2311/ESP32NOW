#!/usr/bin/env python3
"""Check README links, generated diagrams, and settings against the firmware."""
import configparser
from pathlib import Path
import re
import xml.etree.ElementTree as ET

project = Path(__file__).resolve().parents[1]
readme = (project / "README.md").read_text(encoding="utf-8")
assert len(re.findall(r"^```", readme, flags=re.M)) % 2 == 0, "Unclosed code fence"
assert "```mermaid" not in readme, "README should use verified SVG diagrams"

for target in re.findall(r"\]\(([^)]+)\)", readme):
    if target.startswith(("https://", "http://", "#")):
        continue
    assert (project / target).is_file(), f"Broken local link: {target}"

for name in ("system", "handshake", "led-states"):
    source = project / "docs/diagrams" / (name + ".mmd")
    svg = project / "docs/diagrams" / (name + ".svg")
    assert source.is_file(), f"Missing diagram source: {name}"
    root = ET.parse(svg).getroot()
    assert root.tag == "{http://www.w3.org/2000/svg}svg", f"Invalid SVG: {name}"
    assert "viewBox" in root.attrib, f"Missing SVG dimensions: {name}"
    text = " ".join(root.itertext())
    assert "Syntax error" not in text and "Parse error" not in text, name
    assert len(list(root)) > 1, f"Empty diagram: {name}"

config = configparser.ConfigParser()
config.read(project / "platformio.ini")
doc_config = configparser.ConfigParser()
doc_config.read_string(re.search(r"```ini\n(.*?)\n```", readme, re.S).group(1))
for section in doc_config.sections():
    for key, value in doc_config[section].items():
        assert config[section][key] == value, f"Incorrect documented setting: {key}"

main = (project / "src/main.cpp").read_text()
header = (project / "include/range_link.h").read_text()
settings = {
    "LED_PIN": (main, "2"),
    "LED_ACTIVE_LOW": (main, "false"),
    "WIFI_CHANNEL": (main, "1"),
    "kGroup": (header, "0x32B11A01"),
    "kTimeoutMs": (header, "3000"),
    "kBlinkMs": (header, "250"),
}
for name, (source, value) in settings.items():
    assert re.search(rf"\b{name}\s*=\s*{value}\s*;", source), name
    assert re.search(rf"\| `{name}` \|.*?\| `{value}` \|", readme), name
print("README checks passed: local links, SVGs, code fences, board config and firmware settings.")
