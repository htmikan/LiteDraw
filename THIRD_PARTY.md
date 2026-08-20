# Third-Party Notices

LiteDraw uses the following third-party libraries for optional export compression (JPEG/PNG write path only).

## libjpeg-turbo

- **Use:** JPEG encoding when "ファイルサイズを抑える" is enabled on export
- **License:** BSD-3-Clause (includes IJG libjpeg license terms)
- **Project:** https://github.com/libjpeg-turbo/libjpeg-turbo
- **vcpkg port:** `libjpeg-turbo` (mozjpeg は vcpkg レジストリから削除済みのため代替)

## libdeflate

- **Use:** PNG encoding when "ファイルサイズを抑える" is enabled on export
- **License:** MIT License
- **Project:** https://github.com/ebiggers/libdeflate
- **vcpkg port:** `libdeflate`

## Material Design Icons (embedded font)

See README.md — Pictogrammers Material Design Icons, Apache License 2.0.
