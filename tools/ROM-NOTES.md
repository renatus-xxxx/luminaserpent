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

## Tablacus DISK ROM Lite が駄目だった理由 (先行実験)

- ROM 側 DOS は dos2formsx (DOS2モード, v0.24) テンプレートでのみ
  プロンプト・DIR・小物 .COM が正常 (旧 v0.16 はバナー停止、
  DOS1モード v0.26 はキー入力不可)。
- しかし **大きな .COM は COMMAND から起動できない**:
  当初「ロード範囲が 4000h を跨ぐと止まる」とみたが、後の切り分け
  (2026-11-04) で正確な条件は **ファイルサイズ 11,303 バイト以上**
  と判明した。11,302B は正常、11,303B は停止する。COMMAND は
  プログラムを 13DAh のステージング領域へ一括読みするため、
  11,303B でステージング読みの転送先が初めてページ1 (4000h) に
  達する。.COM 自身のロード範囲は無関係 (0100h 直接ロードの自作
  ローダでは 11,776B も正常動作した)。
- ただし読み込み全般が壊れているわけではない。転送先をページ3の
  バッファに取る FCB 連続読みは正常で、GO.COM 経由なら Tablacus
  でもタイトル〜デモプレイまで動作した (2026-11-03 追加検証)。
  作者の README の制限一覧にも GitHub Issues にも記載なし。

### 原因確定と修正 (2026-11-04)

- 原因は DISK ROM Lite v0.7.4.4 側の不具合。ページ1宛てのファイル
  転送 (FILE.ASM の LDIR_PAGE1、256バイトずつバッファ経由で転送)
  の**終了判定命令が、リリースバイナリに含まれていない**。
  LDIR_PAGE1 入口と LDIR_PAGE1_ROM の2箇所で、終了処理は
  `#if exists _RAM64K` の内側にネストした `#if exists
  USE_NORMAL32K_OR_ASCII16 / JP Z,LDIRE2 / #else / RET Z` で
  書かれているが、このネストした #if exists はアセンブラ
  AILZ80ASM (v1.0.31 で再現、未修正ソースのビルドが公式バイナリと
  バイト単位で一致) が展開せず、両分岐とも欠落する。このため
  ページ1に1バイトでも入る転送は終了できず、残量カウンタが
  桁借りして全面コピーの暴走となりハングする。
- 修正は FILE.ASM のみ (作業ツリー `~/workspace/msx/romlab/
  tablacus-full/`、未コミット): 終了処理を非ネスト位置に
  LDIR_PAGE1_FIN として定義し、2箇所から JP Z で飛ばす。
  4バリアントともビルド成功し、検証済み:
  - 11,302B / 11,303B の境界プローブが両方正常動作。
  - LUMINA.COM (49,463B) が COMMAND から直接起動し、タイトル～
    デモプレイを確認 (C-BIOS MSX2+ と実BIOS の FS-A1GT の両方)。
  - データファイル5本 (TPL8/GPL8/GPLF8/LOGO/SPARK) を収録した
    ディスクでは、タイトルロゴ・地球表示・ボス戦を含むデモループ
    全体 (約15分) が完走しタイトルへ復帰することも確認済み。
    なお初期の検証ディスク PB.DSK は実験用でデータファイルが
    入っておらず、ロゴと地球が表示されない動画を一度納品した
    (Lite 側の問題ではない)。
  - 起動経路 (DOS のないディスク／ROM 単体) の挙動は原版と同一。
- なお PR はユーザーの指示で保留中 (送付しない)。AILZ80ASM 側の
  ネスト #if exists の問題として、そちらへ報告する選択肢もある。

## パッチ版 Lite のリポジトリ組み込み (2026-11-04 追記)

上記の「PR は保留中」の記述の後、方針が変わったため経緯を追記する。

- 修正は上流へ PR として提出済み:
  https://github.com/tablacus/TablacusDiskRomLite/pull/3
  (renatus-xxxx の fork、ブランチ fix/ldir-page1-termination、
  コミット 2b2954a)。
- PR が取り込まれない場合に備え、パッチ版 Lite をこのリポジトリに
  同梱して自前で使えるようにした (`tools/tablacus/`):
  - `DISKROM64K.BIN` — パッチ版 (MD5
    `7743f77fa0d635611e413432158315f7`)。原版 v0.7.4.4 は
    `c66f43fca66a2b916ef05fc9580871bf` で、区別のために両方を
    tools/tablacus/README.md に明記した。元の著作権表示
    (LICENSE, MIT, Copyright (c) 2023 Gaku) はそのまま保持。
  - `FILE.ASM.patch` — 原版→修正版の差分 (FILE.ASM のみ)。
  - `DOS2FORMSX.DSK` — ゲームディスクのテンプレート
    (dosformsx DOS2モード。COMMAND.COM / DOS.SYS を含む)。
- ビルドは `make rom-lite` (tools/make_rom_lite.py)。
  ROM = パッチ版 DISKROM64K.BIN (8KB) + ゲームディスク (720KB)
  = 745,472 バイト。ゲームディスクはテンプレートに LUMINA.COM と
  データ5本 (TPL8/GPL8/GPLF8/LOGO/SPARK) を入れ、AUTOEXEC.BAT の
  "LUMINA" で自動起動する、検証済みの構成そのまま。
  現行ビルドでの生成物は romlab で検証済みのイメージとバイト単位
  で一致した (ROM MD5 `130edd511fb469ac6d1dd74b02cada4c`)。
- 生成した LUMINA-LITE.ROM を openMSX で再確認済み (2026-11-04):
  C-BIOS MSX2+ で電源投入→自動起動→タイトル (ロゴ・地球) →
  デモ開始、実BIOS FS-A1GT でも自動起動→デモ進行を確認
  (debug-openmsx/romlite/ にスクリーンショット)。
- 位置づけ: 普段の動作確認はフロッピー、最終確認は ROM という
  従来フローは維持し、ROM は LSX 方式 (`make rom`) と Lite 方式
  (`make rom-lite`) の併存とする。LSX 方式は 128KB に収まり
  実カートリッジ (4Mbit) 向き、Lite 方式は COMMAND から直接
  起動できる素直さが利点。

### Lite ROM の内蔵ディスクを 360KB に統一 (2026-11-04 追記)

- dist の LUMINA-LITE.ROM の最終形は、内蔵ディスクを 360KB
  フォーマット (368,640 バイト) にして本体を 376,832 バイト
  とし、末尾を 0xFF で 524,288 バイト (512KB) までパディング
  した形である。512KB カートリッジ (MSX Simple Mega Flash、
  ASCII 8K) にそのままフラッシュ焼き込みできる形であり、
  WebMSX のプレイリンクと実機への焼き込みで同一のイメージを
  使う。
- `make rom-lite` (tools/make_rom_lite.py) が 360KB ディスクの
  生成と 0xFF パディングまでを行うよう変更した。ブートセクタ
  は dos2formsx テンプレートのものを継承し、BPB の
  TotSec16=720／Media=F8／Heads=1 のみ書き換える (FATSz16 は
  3 のまま)。Lite は BPB を読んでジオメトリを決めるため、
  この小容量フォーマットでも認識される。COMMAND.COM /
  DOS.SYS はテンプレートから抽出して収録し、ファイルの
  並び順は従来どおり。
- 最終形の ROM MD5 は `c32ff4688f00cd89ae9b17d8c4f100ee`
  (パディング込み)。先頭 376,832 バイトは、先行検証で
  openMSX の実BIOS FS-A1GT 構成 (ASCII8) で自動起動→
  タイトル→デモ (ボス戦突入まで) を確認したイメージ
  (MD5 `0a5c3ae273421c85c7470421e3bc5e46`) とバイト単位で
  一致する。
- 360KB 内蔵版は WebMSX でも動作確認済み (2026-11-04):
  無パディング版とパディング版 (dist の最終形そのもの) の
  両方で、turboR + NODISK プリセットで自動起動→タイトル
  完成まで約7〜10秒→デモ進行 (ボス戦突入まで) を確認した。
- 差し替え前の 745,472 バイト版 (720KB 内蔵、MD5
  `130edd511fb469ac6d1dd74b02cada4c`) は廃止。
- .dsk 配布は従来どおり 720KB (2DD) のままで、変更していない。

## フロッピー側

- 現行 dosformsx (DOS1モード, v0.26) のディスクで実BIOS機の
  フロッピー起動からタイトル〜デモまで正常動作を確認済み。
  リポジトリの Makefile が使う TRY16 テンプレート (DOS v0.16) の
  差し替えは別途判断 (ROM 側は LDMSX が OS を持つため無関係)。
