[English version](./README.md)

# luminaserpent

**LUMINA SERPENT** は **MSX2 / MSX2+ / MSX turboR** 用の
オリジナル横スクロールシューティングです。z88dk（C言語）で
開発し、起動可能な MSX-DOS ディスクイメージとして配布して
います。X68000 版と同名の作品の MSX 版で、同じゲームデザイン
を V9938/V9958 向けに再実装したものです。

![LUMINA SERPENT プレビュー — タイトル、地球面、ボス戦](preview.gif)

自機は **光蛇レーザー** を引き連れて飛びます。ビームは自機の
過去の位置履歴から描かれるため、移動に合わせて蛇行します。
曲がれば曲がるほど **RESONANCE** ゲージが溜まり、満タンで
強力な **PRISM BURST** が発動します。敵を倒すと装備アイテムが
現れ、**THRUST / COMET / DUAL / PRISM / ECHO / HALO** の
オリジナル装備がローテーションします。ステージの最後では、
地球を背に **ABYSS PRISM LEVIATHAN** が待ち受けます。コアは
周回する4つの破片に守られています。

## プレイ / ダウンロード

- [WebMSX で遊ぶ (MSXturboR)](https://webmsx.org/?MACHINE=MSXTRJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent/main/dist/LUMINA.DSK&FAST_BOOT)
- [LUMINA.DSK](dist/LUMINA.DSK) — 起動可能な 720KB ディスクイメージ（自動起動）
- [LUMINA.COM](dist/LUMINA.COM) — MSX-DOS プログラム単体

現在のビルド (SCREEN 8 版): `LUMINA.COM` 50,282 bytes、
MD5 `1e28b405fded17ce102b5ae0c2f01cfb`。

## 操作

| 入力 | 動作 |
|---|---|
| カーソルキー / ジョイスティック | 移動 |
| SPACE / トリガー | ショット（押し続けると光蛇レーザー） |
| ESC（ゲーム中） | タイトルへ戻る |
| タイトルで約5秒無入力 | アトラクトデモ |

## 実装について

- **全機種共通の単一バイナリ** — 起動時に MSX2 / MSX2+ /
  turboR を判別し、turboR でのみ R800 に切り替えます。
  BASIC やディスクの準備は不要で、ディスクから直接ゲームが
  起動します。
- **SCREEN 8 (GRAPHIC 7) の 256 色グラフィックス (GRB332)**
  — タイトルとゲーム中の2つの地球は、ディザで階調を出した
  256色レンダリングで、.COM が MSX-DOS のメモリ上限にある
  ため、ディスク上のデータファイル（`TPL8.DAT` /
  `GPL8.DAT`）からストリーム読みします。背景は「Deep
  Field」：滑らかな縦グラデーションの上に2層の手続き生成
  星空を密に敷いた星野です。
- **GRAPHIC 7 でのスプライト色** — このモードではスプライト
  の色コードはプログラム可能パレットではなく VDP 内蔵の
  固定テーブルで解釈されるため、スプライトのパレット番号を
  固定テーブルの最近色へ変換し、旧 SCREEN 5 版の色味を
  維持しています。
- **WARNING 復元の高速化** — ボス前の WARNING パネルを消す
  とき、背後の地球はディスクから読み直さず（約4.3秒）、
  VRAM 内に退避したコピーから復元します（約0.06秒）。
- **画面分割** — ライン割り込みで HUD（スコアと RESONANCE
  ゲージ）と装備バンドを固定し、その間のプレイ画面を描画
  します。
- **PSG の音楽と効果音**（タイトル / 通常面 / ボスの
  3シーン）。

## ビルド

```
make -f Makefile.linux        # SCREEN 8 版（既定）
make -f Makefile.linux dsk    # 起動可能なディスクイメージまで
```

z88dk（クラシックライブラリ、`+msx` ターゲット）が必要です。
既定のビルドは `-DNOSCROLL -DSCR8 -DSCR8LIN` を定義して
`LUMINA.COM` を生成し、`tools/make_dsk.py` がテンプレートの
MSX-DOS ディスクを使って起動可能なディスクイメージにまとめ
ます（SCREEN 8 用の地球データ `data/tpl8.dat` /
`data/gpl8.dat` も収録）。旧 SCREEN 5 版（v1.8 相当）は
`make -f Makefile.linux scr5` で引き続きビルドできます。

## 注意

- MSX2 以降（SCREEN 8、すなわち V9938 以降）が必要です。
  MSX1 には対応していません。
- サウンドは PSG のみです。
