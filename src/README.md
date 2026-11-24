# ソース

- `dll/`: 日本語化の本体（`winmm.dll`）。`build.bat` で作ります（Visual Studio 2022 の x86 のコンパイラを使います）。
  `build.bat test` で試験版（`build/test/winmm.dll`。`tools/win.py` が使います。配布しません）を作ります。
  埋め込むデータ `build/patch.bin`（訳の表、文字の絵、パラグラフの画像）は、`tools/` の道具で作ります。訳の表とフォントはこのリポジトリには入れていません。
- `tools/`: ゲームのデータから文章を取り出す道具、訳の点検、訳の表とパラグラフの画像を作る道具、試験用の道具。
