[English version](./README.md)

# luminaserpent

**LUMINA SERPENT** は **MSX2 / MSX2+ / MSX turboR** 用の
オリジナル横スクロールシューティングです。z88dk（C言語）で
開発し、起動可能な MSX-DOS ディスクイメージとして配布して
います。X68000 版と同名の作品の MSX 版で、同じゲーム
デザインを再実装したものです。

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

現在のビルド :`LUMINA.COM` 49,548 bytes、
MD5 `1dcdd81d71db4960084306e59c9fc4db`。

## 操作

| 入力 | 動作 |
|---|---|
| カーソルキー / ジョイスティック | 移動 |
| SPACE / トリガー | ショット（押し続けると光蛇レーザー） |
| ESC（ゲーム中） | タイトルへ戻る |
| タイトルで約5秒無入力 | アトラクトデモ |

## ビルド

```
make -f Makefile.linux        # 既定
make -f Makefile.linux dsk    # 起動可能なディスクイメージまで
```

z88dk（クラシックライブラリ、`+msx` ターゲット）が必要です。
既定のビルドは `-DNOSCROLL` を定義して `LUMINA.COM` を生成
し、`tools/make_dsk.py` がテンプレートの MSX-DOS ディスクを
使って起動可能なディスクイメージにまとめます（地球データ
`data/tpl8.dat` / `data/gpl8.dat` も収録）。

## 注意

- MSX2 以降（SCREEN 8、すなわち V9938 以降）が必要です。
  MSX1 には対応していません。
- サウンドは PSG のみです。
