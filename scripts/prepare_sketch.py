#!/usr/bin/env python3
"""Stage or package the Arduino sketch. The repo layout is already
Arduino-compatible (.ino + src/), so files are copied as they are."""

import argparse
from pathlib import Path
import shutil
import zipfile

SKETCH = "tando"
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".c"}


def source_files(source):
    sketch = source / "firmware" / SKETCH
    required = [sketch / f"{SKETCH}.ino", source / "VERSION"]
    for path in required:
        if not path.is_file():
            raise ValueError(f"Missing required sketch file: {path}")
    files = [*required]
    files += sorted(p for p in sketch.glob("*") if p.suffix in SOURCE_SUFFIXES)
    src_dir = sketch / "src"
    if src_dir.is_dir():
        files += sorted(p for p in src_dir.rglob("*") if p.suffix in SOURCE_SUFFIXES)
    if (source / "README.md").is_file():
        files.append(source / "README.md")
    return [p for p in files if p.is_file()]


def relative_name(source, path):
    sketch = source / "firmware" / SKETCH
    try:
        return path.relative_to(sketch)
    except ValueError:
        return Path(path.name)  # VERSION, README.md


def stage(source, destination):
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("The staged sketch directory must not overlap the source directory")
    if destination.name != SKETCH:
        raise ValueError(f"The staged sketch directory must be named {SKETCH}")
    files = source_files(source)
    destination.mkdir(parents=True, exist_ok=True)
    for path in destination.iterdir():
        if path.name == "src" and path.is_dir():
            shutil.rmtree(path)
        elif path.is_file() or path.is_symlink():
            if path.suffix in {".ino", ".h", ".hpp", ".cpp", ".c"} or path.name in {"VERSION", "README.md"}:
                path.unlink()
    for path in files:
        target = destination / relative_name(source, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)


def package(source, destination):
    files = source_files(source)
    if destination.suffix != ".zip" or destination in [p.resolve() for p in files]:
        raise ValueError("The package output must be a .zip file, separate from the sources")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, f"{SKETCH}/{relative_name(source, path).as_posix()}")


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