# Code Signing Policy

LiteDraw binaries distributed from this repository are code-signed as follows.

**Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).**

## Scope

- Signed artifacts: `LiteDraw.exe` and bundled runtime DLLs (`deflate.dll`, `jpeg8.dll`) in official release packages.
- Only binaries built from this repository's CI pipeline on the `main` branch are submitted for signing.
- Local or unofficial builds are **not** signed.

## Origin verification

Release builds are produced by GitHub Actions (see [`.github/workflows/build.yml`](.github/workflows/build.yml)). SignPath origin verification ensures that signed binaries match the public source at a specific commit.

## Publisher

The code signing certificate is issued to **SignPath Foundation**. The publisher name shown in Windows SmartScreen / Authenticode is therefore SignPath Foundation, not the individual maintainer.

## Questions

For signing policy questions, contact the project maintainer via GitHub Issues. For SignPath service questions, see [SignPath documentation](https://docs.signpath.io/).
