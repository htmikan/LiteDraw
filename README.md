# LiteDraw

手順書向け画像へ注釈を追加する、Windows専用の軽量画像エディターです。
サードパーティーライブラリを使わず、Win32、Direct2D、DirectWrite、WICで実装しています。

## Visual Studio 2026でのビルド

1. Visual Studio Installerで「C++によるデスクトップ開発」と Windows 10/11 SDK をインストールします。
2. `LiteDraw.slnx` を Visual Studio 2026 で開きます。
3. 構成を `Debug | x64` または `Release | x64` にしてビルドします。
4. `x64\Debug\LiteDraw.exe` または `x64\Release\LiteDraw.exe` を実行します。

Visual Studio の CMake 統合を使う場合は、リポジトリのフォルダーまたは `CMakeLists.txt` を直接開いてもビルドできます。

## UI構成

- 上段: アイコンのみのツールバー。マウスオーバーで名前を表示します。
- 中段: 選択中ツールまたは選択中オブジェクトに応じて切り替わるプロパティ帯。
- 下段: 編集キャンバス。

`assets/fonts/materialdesignicons-webfont.ttf` が見つかれば Material Design Icons を表示し、見つからない場合は文字ラベルへフォールバックします。

## 主な機能

- 開く、保存、名前を付けて保存、書き出し
- 画像縮小: `75%` / `50%` / `25%`
- 選択、矢印、矩形、楕円、フリーハンド、テキスト、ぼかし、モザイク、番号
- 部分拡大ルーペ: 丸/四角、`1.5x` / `2x` / `3x`
- サイズ切り取り: クロップ枠を表示し、枠外を暗転
- Undo / Redo / Delete
- 拡大 / 等倍 / 縮小

## 操作メモ

- テキスト/番号: ツール選択後にキャンバスをクリックすると入力ダイアログを開きます。
- クロップ: 枠の辺やハンドルをドラッグして調整し、`Enter` で確定、`Esc` でキャンセルします。
- 選択: オブジェクトの移動、リサイズ、矢印端点編集に対応します。
- マウスホイール: ズーム
- 中ボタンドラッグ: パン
- `Ctrl+Z` / `Ctrl+Y`: Undo / Redo
- `Delete`: 選択オブジェクトを削除

JPEG 読み込み時は EXIF Orientation をピクセルへ反映し、書き出し画像には EXIF メタデータを付与しません。
