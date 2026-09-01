import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

VALUE_PATTERN = re.compile(
    r'^\s*inline\s+constexpr\s+std::string_view\s+'
    r'(Name|Author|Description|Version)\s*=\s*'
    r'"((?:\\.|[^"\\])*)";\s*$'
)

REQUIRED_VALUES = (
    "Name",
    "Author",
    "Description",
    "Version",
)

ZOOM_NORMAL_ARCHIVE_PATH = (
    "resources/zoom/ic_zoom_normal.rgba"
)

ZOOM_PRESSED_ARCHIVE_PATH = (
    "resources/zoom/ic_zoom_pressed.rgba"
)


def parse_version(
    path: Path
) -> dict[str, str]:

    values: dict[str, str] = {}

    for line in path.read_text(
        encoding="utf-8"
    ).splitlines():

        match = VALUE_PATTERN.match(
            line
        )

        if match:
            values[
                match.group(1)
            ] = bytes(
                match.group(2),
                "utf-8"
            ).decode(
                "unicode_escape"
            )

    missing = [
        name
        for name in REQUIRED_VALUES
        if not values.get(name)
    ]

    if missing:
        raise ValueError(
            "Missing version metadata: "
            + ", ".join(missing)
        )

    return values


def build_manifest(
    values: dict[str, str]
) -> dict[str, object]:

    return {
        "type": "preload-native",
        "name": values["Name"],
        "author": values["Author"],
        "description": values["Description"],
        "version": values["Version"],
        "entry": "libBedrockTools.so",
        "icon": "icon.png",

        "overwrite_files": [
            "icon.png",
            "resources/minecraft.ttf",
            ZOOM_NORMAL_ARCHIVE_PATH,
            ZOOM_PRESSED_ARCHIVE_PATH,
        ],

        "overwrite_folders": [],
    }


def write_package(
    library: Path,
    icon: Path,
    font: Path,
    zoom_normal: Path,
    zoom_pressed: Path,
    version_header: Path,
    output: Path,
) -> None:

    required_files = {
        "Library": library,
        "Icon": icon,
        "Font": font,
        "Zoom normal image": zoom_normal,
        "Zoom pressed image": zoom_pressed,
        "Version header": version_header,
    }

    for label, path in required_files.items():
        if not path.is_file():
            raise FileNotFoundError(
                f"{label} not found: {path}"
            )


    expected_rgba_size = (
        256 *
        256 *
        4
    )


    if (
        zoom_normal.stat().st_size
        != expected_rgba_size
    ):
        raise ValueError(
            "Unexpected Zoom normal RGBA size: "
            f"{zoom_normal.stat().st_size}; "
            f"expected {expected_rgba_size}"
        )


    if (
        zoom_pressed.stat().st_size
        != expected_rgba_size
    ):
        raise ValueError(
            "Unexpected Zoom pressed RGBA size: "
            f"{zoom_pressed.stat().st_size}; "
            f"expected {expected_rgba_size}"
        )


    manifest = build_manifest(
        parse_version(
            version_header
        )
    )


    output.parent.mkdir(
        parents=True,
        exist_ok=True
    )


    if output.exists():
        output.unlink()


    manifest_bytes = (
        json.dumps(
            manifest,
            indent=2,
            ensure_ascii=False
        )
        + "\n"
    ).encode(
        "utf-8"
    )


    with zipfile.ZipFile(
        output,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:

        archive.writestr(
            "manifest.json",
            manifest_bytes
        )

        archive.write(
            library,
            "libBedrockTools.so"
        )

        archive.write(
            icon,
            "icon.png"
        )

        archive.write(
            font,
            "resources/minecraft.ttf"
        )

        archive.write(
            zoom_normal,
            ZOOM_NORMAL_ARCHIVE_PATH
        )

        archive.write(
            zoom_pressed,
            ZOOM_PRESSED_ARCHIVE_PATH
        )


    expected = {
        "manifest.json",
        "libBedrockTools.so",
        "icon.png",
        "resources/minecraft.ttf",
        ZOOM_NORMAL_ARCHIVE_PATH,
        ZOOM_PRESSED_ARCHIVE_PATH,
    }


    with zipfile.ZipFile(
        output,
        "r"
    ) as archive:

        names = set(
            archive.namelist()
        )


        if names != expected:
            raise RuntimeError(
                "Unexpected package entries: "
                f"{sorted(names)}"
            )


        parsed = json.loads(
            archive.read(
                "manifest.json"
            )
        )


        if parsed != manifest:
            raise RuntimeError(
                "Manifest verification failed"
            )


        checks = {
            "libBedrockTools.so":
                library,

            "icon.png":
                icon,

            "resources/minecraft.ttf":
                font,

            ZOOM_NORMAL_ARCHIVE_PATH:
                zoom_normal,

            ZOOM_PRESSED_ARCHIVE_PATH:
                zoom_pressed,
        }


        for (
            archive_name,
            source_path
        ) in checks.items():

            if (
                archive
                .getinfo(
                    archive_name
                )
                .file_size
                !=
                source_path.stat().st_size
            ):
                raise RuntimeError(
                    "Package verification failed: "
                    f"{archive_name}"
                )


def main() -> int:

    parser = argparse.ArgumentParser()


    parser.add_argument(
        "--library",
        required=True,
        type=Path
    )


    parser.add_argument(
        "--icon",
        required=True,
        type=Path
    )


    parser.add_argument(
        "--font",
        required=True,
        type=Path
    )


    parser.add_argument(
        "--zoom-normal",
        required=True,
        type=Path
    )


    parser.add_argument(
        "--zoom-pressed",
        required=True,
        type=Path
    )


    parser.add_argument(
        "--version-header",
        required=True,
        type=Path
    )


    parser.add_argument(
        "--output",
        required=True,
        type=Path
    )


    args = parser.parse_args()


    try:
        write_package(
            args.library.resolve(),
            args.icon.resolve(),
            args.font.resolve(),
            args.zoom_normal.resolve(),
            args.zoom_pressed.resolve(),
            args.version_header.resolve(),
            args.output.resolve(),
        )

    except Exception as error:
        print(
            error,
            file=sys.stderr
        )

        return 1


    print(
        args.output.resolve()
    )


    return 0


if __name__ == "__main__":
    raise SystemExit(
        main()
    )
