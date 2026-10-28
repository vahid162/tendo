#!/usr/bin/env python3
"""Stage or package the complete Arduino sketch without changing its sources."""

import argparse
from pathlib import Path
import shutil
import zipfile


SKETCH = "tando_final_demo_15min"


def source_files(source):
    sketch = source / "firmware" / SKETCH
    required = [sketch / f"{SKETCH}.ino", source / "VERSION"]
    for path in required:
        if not path.is_file():
            raise ValueError(f"Missing required sketch file: {path}")
    files = [*required, *sorted(sketch.glob("*.h")), *sorted(sketch.glob("*.cpp"))]
    if (source / "README.md").is_file():
        files.append(source / "README.md")
    return [path for path in files if path.is_file()]


def stage(source, destination):
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("The staged sketch directory must not overlap the source directory")
    if destination.name != SKETCH:
        raise ValueError(f"The staged sketch directory must be named {SKETCH}")
    files = source_files(source)
    destination.mkdir(parents=True, exist_ok=True)
    # Remove old compilation inputs so deleted or renamed modules cannot linger.
    for path in destination.iterdir():
        if path.suffix in {".ino", ".h", ".cpp"} or path.name in {"VERSION", "README.md"}:
            if path.is_file() or path.is_symlink():
                path.unlink()
    for path in files:
        shutil.copy2(path, destination / path.name)


def package(source, destination):
    files = source_files(source)
    if destination.suffix != ".zip" or destination in [path.resolve() for path in files]:
        raise ValueError("The package output must be a .zip file, separate from the sources")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, f"{SKETCH}/{path.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("stage", "package"))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        operation = stage if args.command == "stage" else package
        operation(args.source.resolve(), args.destination.resolve())
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
