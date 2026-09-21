# 変更履歴

実装ファイル（`.keymap` / `.overlay` / `.conf`）の逐次の変更はコミットログを参照。
ここには「なぜ今の値なのか」「何を踏んだか」を残す。

---

## 2026-09-21  moNa2 キーマップの移植と実機検証

（by チャット依頼 / ブランチ `claude/keymap-mona2-port`）

moNa2 の 14 レイヤー設計を roBa へ移植したが、**実機確認前の状態**だった。
実機に焼いて出てきた不具合と、その原因・対処。

### 1. パン・スクロールが極端に鈍い

スクロール速度は **dts 側の `zip_scroll_scaler` と runtime 側の
`scroll_runtime_input_processor` の積**で決まる。後者はモジュール既定が **1/60**。
この上書きが移植時に脱落していたため、実効倍率が scroller 1/960 / panner 1/320 に
なっていた。

`&scroll_runtime_input_processor` を 1/1 に上書きして「Studio の表示倍率 = 実効倍率」
に揃え、固定減速は `zip_scroll_scaler` に一本化した（scroller `6/15` / panner `6/5`）。

### 2. エンコーダ（左手 CKW12）

移植で初めて回転に `&msc` を割り当てたのが発端。本家も旧ブランチも `&kp` 系だった
ため表面化していなかった。3つの症状が出たが、原因は別々。

**`&msc` は「速度」ベースの behavior**（`zmk,behavior-input-two-axis`）。
押している間 `trigger-period-ms` ごとに `速度 × period / 1000` ずつ動かす。

- 素の `&msc SCRL_DOWN` は 0.16行/tick で、切り捨てられてほぼ 0行 → **反応しない**
  （`tick_work_cb` は移動量 0 のときイベント自体を出さない）
- press で速度を加算・release で減算する実装。`runtime-sensor-rotate` は press/release
  を behavior queue に積み、press が `tap-ms` 分キューを占有するため、速く回すと溢れて
  release だけが落ちる → **速度が 0 に戻らず止まらなくなる**

**`steps` が実機と不一致**。CKW12 は1ノッチあたり約4パルス出るので、本家から継承した
`steps=12` では 1ノッチ = 120度 = 約3.3トリガとなり、音量が1ノッチで3〜4段飛んでいた。
moNa2 で実機確認済みの `48` に合わせた。

**合わせるべきは移動量ではなくイベント数だった**。YouTube のようにホイールで音量を
変える UI は「1イベントあたり一定量（YouTube は 5%）」で動かし、イベントの `value` を
見ない。1ノッチで3イベント出れば 15% 動いてしまう。一方ページのスクロールは `value` を
見るので、**1ノッチ = 1イベント、量は `value` で決める**が唯一の解になる。

現在の値は「1ノッチ → 1トリガ → 1 press/release → 1 tick → 1 イベント」が
一本道になるよう組んである:

| 設定 | 値 | 役割 |
|---|---|---|
| `steps` | 48 | 1パルス = 7.5度 |
| `triggers-per-rotation` | 12 | 境界 30度 = 1ノッチ。10（36度）だと噛み合わずムラになる |
| `&msc` の `trigger-period-ms` | 30ms | press で +30ms に1発目が予約される |
| `scroll_up_down` の `tap-ms` | 45ms | release で2発目（60ms）をキャンセル。前後15ms余裕 |
| `MOVE_Y` | ±100 | 1イベントの value = 3（普通のマウスのノッチ3個分） |

> **触るときの注意**: 量を変えたいときは `MOVE_Y` だけを増減すること。`tap-ms` と
> `trigger-period-ms` の比を崩すと1ノッチのイベント数が変わり、イベント数で量が
> 決まる UI でまた倍になる。

### 3. タイピング振動でオートマウスレイヤー（AML）が暴発する

`require-prior-idle-ms` を 500 → 1000。`temp_layer` はイベントをそのまま `CONTINUE`
するので、このガードが止めるのは**レイヤー切替だけでカーソル移動は素通りする**。
打鍵直後もボールでカーソルは動き、おあずけになるのはクリック層だけなので、伸ばしても
実害が小さい。

`excluded-positions` の意味に注意。ZMK の実装（`handle_position_state_changed`）上
**「押しても AML を解除しないキー」**であって「押している間 AML へ移行しないキー」では
ない。除外されていないキーを押すと AML は即解除される。したがって載せるべきは MOUSE 層
で実際に機能を持つ J K L M . だけで、moNa2 から一緒に移ってきた Q / A / X は誤発火した
AML を次の打鍵で解除する機会を潰していた（特に A は頻出）ので外した。

ZMK / cormoran 版ドライバ側に**移動量のしきい値に相当する設定は無い**（`zip_temp_layer`
のプロパティは上記2つのみ、`scaler` も 0 になった値を素通しする）。これ以上絞るなら
AML タイムアウト短縮か cpi を下げる方向になる。

### 4. Studio に接続できない

**BLE**: `CONFIG_ZMK_STUDIO_LOCKING` は **`y` 必須**。`n` だと
`ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK`（`default y if ZMK_STUDIO_LOCKING
&& ZMK_BLE`）が無効になり、`&studio_unlock` を押しても Web Bluetooth のデバイス選択
リストに出てこない。本家 roBa の `n` を継承していたのが誤り。

> `&studio_unlock` 自体は `ZMK_STUDIO` にしか依存しないので `n` でもビルドはされる。
> **behavior が存在することと機能することは別。**

**USB**: Mac の有線では問題なく接続できたため、ファーム側は正常。Windows 機固有の
問題（下記）。

---

## 未解決・既知の制約

### エンコーダ: 回し始めの1ノッチが空振りすることがある

回し続けている間は出ない。`ec11_trigger.c` は割り込みコールバックの冒頭で A/B 両方の
割り込みを止め、実処理が終わってから戻す。この**目隠し区間**に2遷移入ると
`ec11_sample_fetch` のデコード表が `default: delta = 0` で黙って捨てるため、2パルス
（15度 = 半ノッチ）が消える。15度ずれると累積が 15 → 45 → 45 … と推移するので、
1ノッチ目だけトリガが出ず以降は安定する。

**試して効かなかった手**: 「目隠し区間がシステムワークキューの待ちで伸びている」と
読んで `CONFIG_EC11_TRIGGER_OWN_THREAD=y` を実機で試したが症状は変わらなかった。
**この読みは外れているので再試行しないこと。** 根治にはドライバをフォークして
デコード表を2ステップ遷移対応にする必要がある。

### Windows 機で Studio に接続できない

| 環境 | 結果 |
|---|---|
| Mac + USB（有線） | 接続可 |
| Windows + USB | `Failed to get device information`（ポートは開くが RPC 無応答） |
| Windows + BT | デバイス選択に出ない（`LOCKING=n` が原因。修正済み） |

Mac で通るのでファームの問題ではない。追うなら `usbser.sys` の割り当てなど OS 側から。

**編集は Mac から有線で行う運用が成立する。** BLE でも接続はできるが、物理レイアウトや
多層キーマップのような大きい RPC ペイロードが時間切れになるため、キーマップ・コンボ・
マクロの編集は有線でないと実用にならない。

### USB シリアルポートが2本見える

`xiao_ble` ボード定義（Zephyr 4.1）が `boards/common/usb/cdc_acm_serial.dtsi` を include
しており、ボード自身がコンソール用の CDC ACM（`board_cdc_acm_uart`）を常に1本生やす。
そこへ `studio-rpc-usb-uart` スニペットが RPC 用をもう1本足すため、COM が2つ現れる。
**ログを有効にしていなければボード側は何も喋らないので、2本とも同じ名前で無言**、
見分けがつかない。Studio でボード側を選ぶと失敗する。

消すなら `&board_cdc_acm_uart { status = "disabled"; };` ＋ `CONFIG_CONSOLE=n`
（cormoran 氏自身のモジュールのテスト設定と同じ手法）。**未適用。**

---

## 調整ポイント早見表

| 変えたいもの | 場所 | 備考 |
|---|---|---|
| ホイールのスクロール量 | `config/roBa.keymap` の `MOVE_Y(±100)` | `tap-ms` / `trigger-period-ms` は触らない |
| ホイールのトリガ頻度 | `roBa.dtsi` の `steps` / `triggers-per-rotation` | 1ノッチ = 1トリガを維持する |
| トラックボールのスクロール速度 | `roBa_R.overlay` の `scroller` の `zip_scroll_scaler` | Studio からも実行時調整可 |
| パン速度 | 同 `panner` の `zip_scroll_scaler` | |
| AML の起きにくさ | `roBa_R.overlay` の `require-prior-idle-ms` | 伸ばしてもカーソル移動には影響しない |
| AML のタイムアウト | 同 `&zip_temp_layer 7 500` の 500 | |
| カーソル速度 | `roBa_R.overlay` の `cpi` | 下げるとスクロール/パンも遅くなる |

---

## 付録: ログ付きビルドの作り方

切り分け用に一時的に `roBa_R_logging` を置いていたが、原因判明後に削除した。
再度必要になったとき用に手順だけ残す。再利用ワークフローの `snippet:` は1個しか
渡せないので、2つ使うには `cmake-args` で cmake の `SNIPPET` 変数へ直接渡す:

```yaml
  - board: xiao_ble/nrf52840/zmk
    shield: roBa_R
    cmake-args: '-DSNIPPET="studio-rpc-usb-uart;zmk-usb-logging" -DCONFIG_ZMK_LOG_LEVEL_DBG=y'
    artifact-name: roBa_R_logging
```
