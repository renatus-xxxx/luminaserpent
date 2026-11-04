# ROM 化の検証メモ (LSX-Dodgers で解決)

2026-11-03、openMSX (C-BIOS MSX2+ JP / Panasonic FS-A1GT 実BIOS) で実測。
カートリッジ前提は MSX Simple Mega Flash (ASCII 8K, 最大4Mbit)。

## 結論: LSX-Dodgers 単体 ROM で解決 (MSX2/MSX2+)

> **位置づけ (2026-11-04 決定)**: この LSX 方式は実験的な
> 代替経路であり、配布には使わない。turboR (FS-A1GT) では
> ロード後にハングする既知の制限があり、WebMSX でも自動
> 起動しない (turboR ではプロンプトで停止、MSX2 では
> ハング)。配布用・実機用は Lite 方式 (dist の
> LUMINA-LITE.ROM) を使うこと。以下の LSX の検証記録は
> 参考として残す。

- `make both` で LUMINA.DSK (720KB) と LUMINA.ROM (128KB) が作れる。
  ROM = LDMSX.BIN (16KB, LSX-Dodgers v1.62c) + ディスクイメージを
  最終使用クラスタ (111,616B) で切り、0xFF で埋めたもの。
  最終ビルド MD5 `a672316d07c0a75c299d0aa27ccc3283`。
  カートリッジ設定は SW2 = ASCII 8K、SW1 = 1Mbit。
- 電源投入だけでゲームが自動起動する (後述の GO.COM 経由)。
  C-BIOS MSX2+ でタイトル画面〜デモプレイまで確認済み。
  ゲーム中の .dat 読み込み (BDOS FCB 連続読み) も ROM 上で正常。
- ROM 内のディスクイメージからは DOS.SYS / COMMAND.COM を除去する
  (LSX は自前のコマンド処理系を持つため不要。フロッピー側には残る)。

## LSX-Dodgers 固有の注意点 (実測)

- カーネルの配置: BDOS 入口 (0006h のワード) は **CB06h**。
  つまりカーネルは CB06h 以降を占有する。TPA は CB06h まで。
  LUMINA.COM のイメージ末尾 C227h は辛うじてその下に入る。
  当初スタブを D000h に置いてカーネルを上書きし、異常動作した。
- AUTOEXEC.BAT の仕組み自体は有効 (小さい .COM は実行できる) が、
  **約49KB のプログラムをバッチから起動すると "Error!" になる**。
  対話モードで同じプログラムを打つと正常に読める。
- 回避策: 自己配置換えスタブ `GO.COM` (304B、tools/lsx/)。
  AUTOEXEC.BAT は "GO" を呼び、スタブが LUMINA.COM を BDOS で
  自前ロードして 0100h へ跳ぶ。本体は C300h (ゲームイメージ末尾
  C227h の直上、カーネル CB06h の下) へ再配置して動く。
- **削除エントリ (0xE5) の罠**: LSX の FCB ファイル検索は、ルート
  ディレクトリで最初に見つけた削除エントリで打ち切られる。
  DIR は全ファイルを表示するのに、その後ろにあるファイルは
  Open できない。make_dsk.py の作るフロッピーイメージは
  テンプレート由来の削除エントリを含み、AUTOEXEC.BAT と
  LUMINA.COM がその後ろにあったため ROM では開けなかった。
  make_rom.py は ROM 化の際にルートディレクトリを圧縮
  (実エントリだけを前に詰め直す) して回避する。
- **turboR (FS-A1GT) では現状ハングする**: LSX 起動と GO.COM の
  ロード完了 (実測で全レコード読み切り) までは進むが、ゲームが
  初期画面 (濃紺一色) で止まる。同じゲームは turboR でも
  フロッピー (似非DOS) からは動作し、LSX でも MSX2+ では動作
  するため、turboR + LSX 環境でのゲーム側タイミング問題と
  見られるが原因は未特定。対象は MSX2 以降 (MSX2/MSX2+ で検証
  済み) とし、turboR は既知の制限とする。

