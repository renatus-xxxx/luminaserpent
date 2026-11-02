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

背景スクロールは MSX2+ / MSX turboR（V9958）に対応して
います。MSX2（V9938）では背景が固定になりますが、ゲーム
内容は同一です。

## プレイ / ダウンロード

- [WebMSX で遊ぶ (MSXturboR)](https://webmsx.org/?MACHINE=MSXTRJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/luminaserpent/main/dist/LUMINA.DSK&FAST_BOOT)
- [LUMINA.DSK](dist/LUMINA.DSK) — 起動可能な 720KB ディスクイメージ（自動起動）
- [LUMINA.COM](dist/LUMINA.COM) — MSX-DOS プログラム単体

現在のビルド :`LUMINA.COM` 49,434 bytes、
MD5 `0c7a0dcf0900a720d873215296874718`；
`LUMINA.DSK` MD5 `b732bfbd03f73ae7377b83c5ec88d873`。

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
既定のビルドは `LUMINA.COM` を生成し、`tools/make_dsk.py` が
テンプレートの MSX-DOS ディスクを使って起動可能なディスク
イメージにまとめます（データファイル `data/tpl8.dat` /
`data/gpl8.dat` / `data/gplf8.dat` / `data/logo.dat` /
`data/spark.dat` も収録）。

## 注意

- MSX2 以降（SCREEN 8、すなわち V9938 以降）が必要です。
  MSX1 には対応していません。
- V9958（MSX2+ / turboR）では背景がハードウェアスクロール
  します。V9938（MSX2）では背景は静止したままになります
  （ゲーム内容は同一です）。
- サウンドは PSG のみです。
