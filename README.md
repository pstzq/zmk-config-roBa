# zmk-config-roBa

[roBa](https://github.com/kumamuk-git/zmk-config-roBa) (kumamuk-git 氏) の
個人設定。ZMK v0.4 世代 (Zephyr 4.1) + [DYA Studio](https://studio.dya.cormoran.works)
対応に移行し、キーマップは同じ owner の
[moNa2](https://github.com/pstzq/zmk-config-moNa2-v2) の設計を移植してある。

## キーマップ (JIS-OS 前提)

<img src="keymap-drawer/roBa_jis.svg">

> OS 側のキーボード配列を **JIS** にして使う前提。ZMK は US(ANSI) 基準の
> HID コードを送るため、記号の対応付けは `config/jis.h` で吸収している。
> US(ANSI) 基準のまま描いた図は `keymap-drawer/roBa.svg`。

## レイヤー

| # | 名前 | 入り方 | 役割 |
|---|---|---|---|
| 0 | MAC | 既定 / BLE層 BT0 | ベース (Mac) |
| 1 | WIN | BLE層 BT1・BT2 | ベース (Windows) |
| 2 | O24 | Q+P コンボでトグル | 代替アルファ配列 |
| 3 | SYM | Space 長押し | 記号 |
| 4 | NUM | Enter 長押し | 数字 (右手テンキー) |
| 5 | NAV-M | 英数 or かな 長押し (Mac) | 矢印・ナビ・メディア |
| 6 | NAV-W | 英数 or かな 長押し (Win) | 矢印・ナビ・メディア |
| 7 | MOUSE | ボールを動かすと自動 (500ms 静止で復帰) | マウスボタン |
| 8 | BLE | 英数+かな コンボ長押し | BT 切替・OS 切替・Studio 解錠 |
| 9 | PAN | P 長押し + ボール | 2D 自由スクロール |
| 10 | SNAP-M | Q 長押し + ボール | ウィンドウ整列 (Mac / Rectangle) |
| 11 | SNAP-W | Q 長押し + ボール | ウィンドウ整列 (Win+矢印) |
| 12 | CURSOR | — | 予約 (未使用) |
| 13 | CLICK | Del 長押し or caps キー長押し | 両手クリッククラスタ |

OS の切替は BT プロファイルと連動している (BT0 = Mac / BT1・BT2 = Win)。

## roBa 固有の事情

### moNa2 (42キー) との差は1キーだけ

マトリクスはどちらも 11列×4行で、roBa の1行目に `RC(1,5)` (左手内側・ホーム段) が
1個多いだけ。したがってキー位置は

```
moNa2 の i  ->  roBa の (i < 15 ? i : i + 1)
```

で機械的に移せる。増えた位置15 には、moNa2 ではコンボ (S+D) に追いやられていた
**Tab** を置いた (コンボ側も従来どおり残してある)。

### 左手サムのホイール (CKW12)

- **回転** … `sensor-bindings`。既定はベース層でスクロール / NAV・BLE 層で音量。
  `zmk-behavior-runtime-sensor-rotate` を使っているので **DYA Studio から
  レイヤーごとに割当を変更できる**。本家の PgUp/PgDn とタブ切替は
  `rsr_pg` / `rsr_tab` として `config/roBa.keymap` に残してある
- **押下** … キーマトリクスの `RC(3,5)` = キー位置39 のただの通常キー。
  moNa2 の位置38 と同じ `&lt NAV_* LANG2` (タップ=無変換 / ホールド=NAV層) で、
  本家の `&lt_to_layer_0 3 LANGUAGE_2` と同じ意味のまま移行できている

### トラックボール (PMW3610)

`cormoran/zmk-driver-pmw3610-with-custom-studio-rpc` を使う。ZMK v0.4 で
Zephyr 4.1 が純正 `pixart,pmw3610` を取り込んだため、本家 (kumamuk-git 版) の
compatible は衝突して使えない。

これに伴い、本家がドライバの devicetree プロパティで持っていた機能は
**入力プロセッサ側へ移った**:

| 本家 (v0.3) | 現構成 (v0.4) |
|---|---|
| `automouse-layer = <4>` | `&zip_temp_layer 7 500` |
| `scroll-layers = <5>` | `scroller { layers = <4 5 6>; }` |
| `CONFIG_PMW3610_CPI` | devicetree の `cpi` プロパティ |
| `CONFIG_PMW3610_SCROLL_TICK` | `&zip_scroll_scaler 1 16` |
| `CONFIG_PMW3610_INVERT_SCROLL_X` | `&zip_scroll_transform INPUT_TRANSFORM_X_INVERT` |
| `CONFIG_PMW3610_ORIENTATION_180` | 相当物なし → `_INVERT_X` / `_INVERT_Y` の組合せ |

慣性スクロールもドライバ機能ではなく入力プロセッサ (`&inertial_scroll`) で足している。
**`&inertial_scroll` は `zip_scroll_scaler` の「前」に置くこと** —
scaler には端数を蓄積する仕組みが無いため、後ろに置くと切り捨てで 0 になった値に
慣性をかけることになり「ちょびちょび・カクカク」になる。

## DYA Studio

<https://studio.dya.cormoran.works> から、キーマップに加えて

- コンボ (8スロット使用済み / 16スロットまで)
- マクロ (8個)
- トラックボールの CPI・軸・スクロール倍率
- ホイール回転の割当 (レイヤーごと)
- 診断タブ (キースキャン / 入力ストリーム / スタック使用量 / デバイス情報)

を実行時に編集できる。

> **キーマップ / コンボ / マクロのタブは USB 接続で開くこと。**
> BLE でも接続自体はできるが、物理レイアウトや多層キーマップのような
> 大きい RPC ペイロードが時間切れになり "Operation timed out" になる。

> Studio で編集した項目はフラッシュ側に保存され、以後 `.keymap` を書き換えても
> **反映されない**。ファーム側の値に戻すには Web UI の "Reset to Default" か
> `settings_reset.uf2` を使う。

## 書き込み

Actions の成果物 (`roBa_R` / `roBa_L` / `settings_reset`) を使う。
世代を跨ぐ更新なので、**先に左右とも `settings_reset.uf2` を焼いてから**
`roBa_L.uf2` / `roBa_R.uf2` を焼くこと。

## ファイル

| パス | 内容 |
|---|---|
| `config/roBa.keymap` | キーマップ本体 (14層 + コンボ + マクロ + behavior) |
| `config/jis.h` | JIS-OS 向け記号エイリアス |
| `config/west.yml` | 依存モジュール (全て SHA 固定) |
| `boards/shields/roBa/roBa_R.overlay` | トラックボールと入力プロセッサチェーン |
| `boards/shields/roBa/roBa_*.conf` | Kconfig (右=中央 / 左=周辺) |
| `keymap_drawer.config.yaml` | 図の描画設定 |
| `scripts/drawer_enrich.py` | 図にコンボと読める見出しを注入 |
| `scripts/jis_relabel.py` | 図のラベルを JIS グリフへ差し替え |
