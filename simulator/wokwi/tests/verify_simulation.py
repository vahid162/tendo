"""Conservative smoke checks; passing these does not certify animation quality."""

import argparse
import json
import re
from pathlib import Path

from PIL import Image, ImageChops


def verify_serial(serial, version):
    if re.search(r"Guru Meditation|panic|Backtrace:|FATAL:|abort\(\)|Brownout detector|Stack smashing", serial, re.I):
        raise ValueError("ESP32 crash/fatal error in Serial log; inspect the log before retrying")
    lines = {line.strip() for line in serial.splitlines()}
    required = (
        "EYES READY", "REACTION LED READY", f"TANDO FIRMWARE v{version}",
        "TANDO FINAL 30-MIN DEMO READY", "PET REACTION - CLEAR AFFECTIONATE UP LOOK",
        "FOOD TAG 1 ACCEPTED", "FOOD REACTION",
        "SLEEP STATE ENTERED - WAITING FOR TAG REMOVAL", "SLEEP TAG REMOVED -> WAKE UP",
    )
    missing = [line for line in required if line not in lines]
    if missing:
        raise ValueError("Missing boot/event evidence: " + ", ".join(missing))
    warnings = [line for line in serial.splitlines() if "spiAttachMISO" in line]
    other_spi = [line for line in serial.splitlines()
                 if re.search(r"spi.*(?:error|fail)", line, re.I) and "spiAttachMISO" not in line]
    if other_spi:
        raise ValueError("SPI failure in Serial log; inspect display transport")
    return warnings


def load_frame(path):
    try:
        with Image.open(path) as source:
            if source.format != "PNG" or source.size != (240, 240):
                raise ValueError(f"{path.name}: expected a 240x240 PNG framebuffer")
            frame = source.convert("RGBA")
    except (OSError, ValueError) as error:
        raise ValueError(f"{path.name}: missing, corrupt, or invalid screenshot") from error
    pixels = list(frame.getdata())
    opaque = sum(alpha >= 240 for _, _, _, alpha in pixels) / len(pixels)
    lit = sum(max(r, g, b) >= 50 and a >= 240 for r, g, b, a in pixels) / len(pixels)
    cyan = sum(g > 100 and b > 100 and r < g * 0.85 and a >= 240
               for r, g, b, a in pixels) / len(pixels)
    # The Tando eye has a dark surround and a cyan body in both sampled poses.
    if opaque < 0.95 or not 0.01 <= lit <= 0.80 or cyan < 0.005:
        raise ValueError(f"{path.name}: blank/transparent or unexpected eye colors; visual review required")
    return frame.convert("RGB")


def verify_images(folder):
    result = {}
    for side in ("left", "right"):
        idle = load_frame(folder / f"{side}-idle.png")
        pet = load_frame(folder / f"{side}-pet.png")
        difference = ImageChops.difference(idle, pet)
        changed = sum(max(pixel) >= 20 for pixel in difference.getdata()) / (240 * 240)
        if changed < 0.005:
            raise ValueError(f"{side}: PET screenshot is unchanged; verify Serial input and SPI")
        result[side] = {"changed_fraction": round(changed, 4)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial-log", type=Path, required=True)
    parser.add_argument("--screenshots", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        warnings = verify_serial(args.serial_log.read_text(encoding="utf-8-sig"), args.version)
        images = verify_images(args.screenshots)
    except (OSError, ValueError) as error:
        parser.exit(1, f"[FAIL] Simulation evidence: {error}\n")
    print(json.dumps({"serial": "passed", "image_smoke": "passed", "eyes": images,
                      "warnings": warnings, "visual_review": "required"}))


if __name__ == "__main__":
    main()
