# LiteDraw

手順書向け画像へ注釈を追加する、Windows専用の軽量画像エディターです。
UI と描画は Win32、Direct2D、DirectWrite、WIC で実装しています。書き出し時の JPEG/PNG 圧縮には vcpkg 経由で libjpeg-turbo と libdeflate をリンクします（オプション、初期 OFF）。

**English:** LiteDraw is a lightweight Windows image annotation editor for technical documentation. Open PNG/JPEG/BMP, add shapes, arrows, callouts, blur/mosaic, and export. Freeware under the [MIT License](LICENSE). Code signing policy: [CODE_SIGNING.md](CODE_SIGNING.md). Function reference: [docs/FUNCTIONS.md](docs/FUNCTIONS.md).

バージョン: `20260820`  
GitHub: https://github.com/htmikan/LiteDraw  
License: [MIT](LICENSE)

## Visual Studio 2026でのビルド

1. Visual Studio Installerで「C++によるデスクトップ開発」と Windows 10/11 SDK をインストールします。
2. Visual Studio 2026 には vcpkg が同梱されています。`LiteDraw.vcxproj` で manifest モード（`VcpkgEnableManifest`）を有効にしているため、別途 vcpkg のインストールは不要です。
3. リポジトリ直下の `vcpkg.json`（libjpeg-turbo / libdeflate）を manifest として自動取得します。Visual Studio 2026 同梱の vcpkg が利用されます。初回ビルド時に依存ライブラリのダウンロードとビルドが走ります。
4. `LiteDraw.slnx` を Visual Studio 2026 で開きます。
5. 構成を `Debug | x64` または `Release | x64` にしてビルドします。
6. `x64\Debug\LiteDraw.exe` または `x64\Release\LiteDraw.exe` を実行します。

Visual Studio の CMake 統合を使う場合は、リポジトリのフォルダーまたは `CMakeLists.txt` を直接開いてもビルドできます（同じ `vcpkg.json` を参照します）。

サードパーティーライブラリのライセンスは [THIRD_PARTY.md](THIRD_PARTY.md) を参照してください。  
コード署名（SignPath Foundation）の方針は [CODE_SIGNING.md](CODE_SIGNING.md)、申請手順は [docs/SIGNPATH_APPLICATION.md](docs/SIGNPATH_APPLICATION.md) を参照してください。

ツールバーとスタンプのアイコンは `assets/fonts/materialdesignicons-webfont.ttf`（約 1.25MB）をリソースに埋め込み、実行時にグリフを直接描画しています。ボタンは `AddFontMemResourceEx` で登録したフォント、キャンバス上のスタンプは DirectWrite のインメモリフォントコレクションで描きます。フォントの読み込みに失敗した場合は文字ラベルへフォールバックします。アイコンは Pictogrammers の Material Design Icons（Apache License 2.0）です。

ウィンドウと exe のアイコンは `src/app.ico` をリソースに埋め込んでいます。

色1 / 色2 は exe と同じフォルダーの `LiteDraw.ini` に保存します。レジストリは使いません。初期値は色1=`E53935FF`、色2=`FFFFFFFF` です。

書き出しの「ファイルサイズを抑える」は、書き出しダイアログ内のチェックボックスで切り替えます（**書き出し形式が BMP のときのみ無効**）。書き出し形式の初期値は入力ファイルの形式（JPEG→JPEG、BMP→BMP、それ以外→PNG）に合わせます。設定は `LiteDraw.ini` の `[Export]` セクション `Lightweight=0|1` に保存されます（初期 OFF）。ON のとき JPEG は WIC（quality 0.65）と libjpeg-turbo（quality 55）の小さい方、PNG は libdeflate（適応フィルタ・level 12、不透明なら RGB）と WIC の小さい方を使います。BMP と上書き保存は常に WIC です。

## UI構成

- 1行目: 開く、上書き保存、レイヤーを保存、書き出し、サイズ縮小、切り取り、選択、直線、矢印、矩形、楕円、フリーハンド、テキスト、吹き出し、部分拡大
- 2行目: ぼかし、モザイク、番号、Undo、Redo、削除、拡大、等倍、縮小、ヘルプ、終了
- 中段: 選択中ツールまたは選択中オブジェクトのプロパティ帯。色1 / 色2 ボックスは常時表示します。
- 下段: 編集キャンバス。

ツールバーボタンは 80px（グリフ 56px）、スタンプ選択ボタンはその 0.5 倍の 40px（グリフ 28px）です。起動時のウィンドウ幅はリボン全体の幅に合わせます。マウスオーバーで名前を表示します。

## 主な機能

- 開く、上書き保存（元画像へ書き戻し）、レイヤーを保存（`.ldl`）、PNG / BMP / JPEG 書き出し（JPEG/PNG はオプションで軽量エンコード）
- 画像縮小: `75%` / `50%` / `25%`（適用ボタンで確定。変更後サイズを表示）
- 選択、直線、矢印、矩形、楕円、フリーハンド、テキスト、吹き出し、部分拡大、ぼかし、モザイク、番号
- サイズ切り取り: クロップ枠を表示し、枠外を暗転。適用ボタンまたは `Enter` で確定
- Undo / Redo / Delete
- 拡大 / 等倍 / 縮小

## 操作メモ

- テキスト / 番号: ツール選択後にキャンバスをクリックして直接入力します。
- 吹き出し: ドラッグで角丸本体を決め、マウスを離したあとに三角の先端をクリックし、本体中央へ文字を入力します。文字が長いと吹き出しサイズを自動調整します。色1は枠線、背景色チェックで色2の塗り、透明度は塗りだけに効きます。
- 部分拡大: 元枠を指定すると近くに拡大表示されます。配置後も元枠の位置・サイズを変えられ、拡大先は独立して移動できます。補助線は対角2本です。
- 矢印 / 矩形 / 楕円 / フリーハンド / 直線 / テキスト: 線幅スライダー（1–32）は本体（色1）。周囲色チェックで色2の縁取り、縁取り幅スライダー、透明度（10–100）は縁取りだけです。テキストの線幅は文字の縁取り基準幅です。直線は実線・破線・一点鎖線・二点鎖線を選べます。矩形・楕円・フリーハンドは破線チェックがあります。
- 番号: 円の塗りは色1。文字色チェックなしなら白、ありなら色2。周囲色はありません。直径は 18–128 です。
- クロップ: 枠の辺やハンドルをドラッグして調整し、適用または `Enter` で確定、`Esc` でキャンセルします。
- 終了時に未保存変更がある場合: 変更破棄して終了 / レイヤーを保存 / 上書き保存 / キャンセル。
- マウスホイール: ズーム
- 中ボタンドラッグ: パン
- `Ctrl+Z` / `Ctrl+Y`: Undo / Redo
- `Delete`: 選択オブジェクトを削除

対応画像は PNG（先頭 `89 50 4E 47 0D 0A 1A 0A`）、JPEG（`FF D8 FF`）、BMP（`BM`）です。マジックバイト不一致、デコード失敗、1px未満、16384px超などは「未対応のフォーマットです」と表示します。JPEG 読み込み時は EXIF Orientation をピクセルへ反映し、書き出し画像には EXIF メタデータを付与しません。

## 利用規約

LiteDraw はフリーウェアです。本ソフトウェアの開発には Cursor (AI) を使用しています。本ソフトウェアは現状有姿 (AS IS) で提供され、明示または黙示を問わずいかなる保証もありません。利用に伴う損害について、作者は責任を負いません。利用は自己責任でお願いします。
