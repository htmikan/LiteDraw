# SignPath Foundation 申請ガイド（LiteDraw）

この文書は [SignPath Foundation](https://signpath.org/) への無償コード署名申請に必要な情報をまとめたものです。申請フォームは英語で入力します。

## 事前チェックリスト

| 項目 | LiteDraw の状態 |
|------|----------------|
| OSI 承認ライセンス | MIT（[`LICENSE`](../LICENSE)） |
| 公開リポジトリ | https://github.com/htmikan/LiteDraw |
| 機能説明 | [`README.md`](../README.md)（英語要約あり） |
| 第三者ライセンス | [`THIRD_PARTY.md`](../THIRD_PARTY.md) |
| コード署名ポリシー | [`CODE_SIGNING.md`](../CODE_SIGNING.md) |
| CI ビルド | [`.github/workflows/build.yml`](../.github/workflows/build.yml) |
| 既にリリース済み | GitHub Releases に exe を公開してから申請 |
| メンバー MFA | GitHub アカウントで 2FA を有効化 |

## 申請 URL

https://signpath.org/ （「Apply for Free Code Signing」）

## フォーム入力例（英語）

### Project / Repository URL

```
https://github.com/htmikan/LiteDraw
```

### License

```
MIT License (see LICENSE file in repository root)
```

### Download / Release URL

```
https://github.com/htmikan/LiteDraw/releases
```

（初回は Release を作成し、`LiteDraw.exe` と同梱 DLL を ZIP で公開してから申請してください。）

### Project description（短文）

```
LiteDraw is a lightweight Windows image annotation editor for technical documentation.
Users can open PNG/JPEG/BMP images, add shapes, arrows, callouts, blur/mosaic regions,
and export annotated images. Built with Win32, Direct2D, DirectWrite, and WIC.
Optional export compression uses libjpeg-turbo and libdeflate (vcpkg).
Distributed as freeware under the MIT License.
```

### Project description（詳細・必要な場合）

```
LiteDraw targets authors of manuals and procedures who need quick image markup on Windows.
Features include: open/save/overwrite, layer save (.ldl), PNG/BMP/JPEG export with optional
size optimization, crop/resize, undo/redo, zoom/pan, and annotation tools (line, arrow,
rectangle, ellipse, freehand, text, callout, loupe, blur, mosaic, numbered stamps).

The application UI is Japanese. Source code, build scripts (MSBuild/vcpkg manifest),
and GitHub Actions CI are public. Binaries are built only from the main branch via CI.
Code signing policy is documented in CODE_SIGNING.md.
```

## 承認後の設定手順（概要）

1. SignPath からプロジェクト作成の案内を受ける。
2. SignPath プロジェクトに GitHub リポジトリ URL を登録する。
3. Trusted Build System として **GitHub Actions** をリンクする。
4. Signing policy で以下を設定する:
   - **Verify origin**: 有効
   - **Allowed branch names**: `main`
   - 署名対象: `LiteDraw.exe`（必要に応じて同梱 DLL）
5. GitHub Actions ワークフローに SignPath の署名ステップを追加する（SignPath ドキュメントの GitHub Actions 連携を参照）。
6. 各リリースは SignPath 上で手動承認後に署名される。

## GitHub Actions への署名ステップ追加（承認後）

SignPath から API トークンとプロジェクト設定を受け取ったら、リポジトリ Secrets に登録し、`build.yml` のビルド後に SignPath の公式 Action を追加します。具体的な Action 名とパラメータは SignPath 管理画面の案内に従ってください。

## 配布 ZIP の推奨構成

```
LiteDraw-20260820-x64.zip
├── LiteDraw.exe
├── deflate.dll
├── jpeg8.dll
├── LICENSE
├── THIRD_PARTY.md
└── README.md（または README へのリンク）
```

## 参考リンク

- [SignPath Foundation Terms](https://signpath.org/terms.html)
- [Origin Verification](https://docs.signpath.io/origin-verification/)
- [Setting up Projects](https://docs.signpath.io/projects/)
