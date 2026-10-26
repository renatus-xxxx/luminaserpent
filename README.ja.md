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

- [WebMSX で遊ぶ (MSX2+)](https://webmsx.org/?MACHINE=MSX2PJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent/main/dist/LUMINA.DSK&FAST_BOOT)
- [LUMINA.DSK](dist/LUMINA.DSK) — 起動可能な 720KB ディスクイメージ（自動起動）
- [LUMINA.COM](dist/LUMINA.COM) — MSX-DOS プログラム単体

現在のビルド (v1.8): `LUMINA.COM` 49,922 bytes、
MD5 `2b697ee2656054d2d0ebbf5ef1772a88`。

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
- **画面分割** — ライン割り込みで HUD（スコアと RESONANCE
  ゲージ）と装備バンドを固定し、その間のプレイ画面を描画
  します。
- **PSG の音楽と効果音**（タイトル / 通常面 / ボスの
  3シーン）。
- デモベンチマーク: MSX turboR で約48fps。

## ビルド

```
make -f Makefile.linux
```

z88dk（クラシックライブラリ、`+msx` ターゲット）が必要です。
ビルドで `LUMINA.COM` が生成され、`tools/make_dsk.py` が
テンプレートの MSX-DOS ディスクを使って起動可能なディスク
イメージにまとめます。

## 注意

- MSX2 以降（SCREEN 5）が必要です。MSX1 には対応して
  いません。
- サウンドは PSG のみです。
