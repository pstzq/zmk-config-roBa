# 変更履歴

このファイルはキーマップ設計・実機検証の経緯を記録します。
実装ファイル（`.keymap` / `.overlay` / `.conf`）の逐次の変更はコミットログを参照してください。

フォーマット：
```
## YYYY-MM-DD  タイトル
- 変更内容（by GitHub 編集 / by チャット依頼）
```

---

## 2026-09-21  moNa2 キーマップの移植と実機検証

（by チャット依頼 / ブランチ `claude/keymap-mona2-port`）

### 背景

roBa を ZMK v0.4 世代（Zephyr 4.1）+ DYA Studio 対応へ移行したうえで、同じ owner の
[moNa2](https://github.com/pstzq/zmk-config-moNa2-v2) の 14 レイヤー設計を移植した。
移植そのものは机上で完了していたが **実機確認は未実施**で、その状態で実機に焼いて
出てきた不具合を潰していったのがこの記録。

移植のコミット（実機確認前）:

| コミット | 内容 |
|---|---|
| `abe4aac` | DYA Studio 2026/08版に対応（ZMK v0.4 世代移行） |
| `25468bc` | 左手ホイールの回転割当を Studio から変更可能に |
| `d840ee1` | moNa2 のキーマップ設計を roBa へ移植（14層 / JIS / トラックボール） |
| `5807295` | キーマップ図の生成を moNa2 と同じ構成に |
| `dbf9eb0` | Release ワークフローを追加 |

---

### 実機検証で出た指摘と原因

#### 1. パン・スクロールが極端に鈍い → 解決

`abe4aac` で入れてあった `&scroll_runtime_input_processor` の 1/1 上書きが、
キーマップ移植 `d840ee1` で `&zip_temp_layer` の追記と差し替わる形で**消えていた**。
このモジュールの既定は **1/60** なので、`zip_scroll_scaler` と掛かって実効倍率が
scroller 1/960、panner 1/320 になっていた。

上書きを復活させ、固定減速は `zip_scroll_scaler` 側に一本化。倍率は moNa2 の実機
調整値を cpi 差（moNa2 800 / roBa 400）で換算したうえ、実機の手応えで再調整した。

| | 移植直後 | 最終 |
|---|---|---|
| `scroller`（NUM/NAV層） | 1/16 | **6/15** |
| `panner`（PAN層） | 3/16 | **6/5** |
| `scroll_runtime_input_processor` | （上書き無し = 1/60） | **1/1** |

> **教訓**: スクロール速度は「dts 側の `zip_scroll_scaler`」と「runtime 側の
> `scroll_runtime_input_processor`」の**積**で決まる。後者はモジュール既定が 1/60。
> 1/1 に上書きして「Studio の表示倍率 = 実効倍率」に揃えておくこと。

コミット: `b66f62b`, `a16c7f1`

---

#### 2. エンコーダ: 反応が悪い / たくさん回すと入りっぱなし → 解決

本家 main は `&inc_dec_kp`、旧 `dya-studio` ブランチは `&rsr_pg`（= `&kp`）で、
**どちらも `&kp` 系**だった。今回の移植で初めて回転に `&msc` を割り当てたのが原因。
デバイスツリー側（GPIO / `steps` / `triggers-per-rotation`）は 3 ブランチとも同一
だったので、ハード設定は無関係だった。

`&msc` の実体は `zmk,behavior-input-two-axis` で、押している間「**速度**」で
スクロールし続ける behavior:

- 1tick の移動量 = `速度 × trigger-period-ms / 1000`。`SCRL_DOWN` は速度10・tick16ms
  なので 0.16行/tick。`tap-ms=100` でも合計 0.96行で、整数への切り捨てと離し際の
  端数破棄によりほぼ 0行 → **「反応が悪い」**
  （`tick_work_cb` は移動量が 0 のときイベント自体を出さない）
- press で速度を**加算**、release で**減算**する実装。`runtime-sensor-rotate` は
  press/release を ZMK の behavior queue へ積み、press が `tap-ms` 分キューを占有
  するため、速く回すと溢れて release だけが落ちる。すると速度が 0 に戻らず
  → **「スクロールが止まらない」**

`&kp` ならリリースを落としても次の打鍵で解消するので、本家や旧ブランチでは表面化
しなかった。

コミット: `b66f62b`

---

#### 3. エンコーダ: 1ノッチで音量が数段飛ぶ → 解決

ZMK 側の計算:

```
EC11 ドライバ    : val1 = pulses * 360 / steps  [度]
sensor-rotate 側 : trigger_degrees = 360 / triggers-per-rotation
behavior         : for (i = 0; i < triggers; i++) で press/release を反復
```

本家から引き継いだ `steps = <12>` が実機と合っていなかった。CKW12 はクリック1つ
あたり**約4パルス**出るため、12 だと 1ノッチ = 4 × 30度 = 120度 = 約3.3トリガに
なり、1ノッチで音量が3〜4段飛ぶ。

同じ CKW12 を積む moNa2 で実機確認済みの `48` に合わせた。

コミット: `56dd9e9`

---

#### 4. YouTube で音量が 5% にならない（15% / 10% / 無反応） → 解決

**これが一番の遠回りだった。** 「1ノッチあたりの移動量の合計」を合わせようとして
いたが、合わせるべきは**イベントの数**だった。

YouTube のようにホイールで音量を変える UI は「1イベントあたり一定量（YouTube は
5%）」で動かし、イベントの `value`（移動量）を見ない。1ノッチで3イベント出れば
15% 動いてしまう。

数字も合う:

| | 1ノッチあたりのイベント数 | YouTube での変化 |
|---|---|---|
| 移植直後（steps=12、約3.3トリガ × 約2イベント） | 約6 | 30% |
| steps=48 直後（0〜1トリガ × 2〜4イベント） | 0 / 2 / 3 | 無反応 / 10% / 15% |
| 最終 | **1** | **5%** |

`&msc` は押している間 `trigger-period-ms` ごとにイベントを出し続けるので、
`tap-ms` の窓に**ちょうど1tickだけ**入るよう組んだ。

| 設定 | 値 | 意味 |
|---|---|---|
| `&msc` の `trigger-period-ms` | **30ms** | press で +30ms に1発目が予約される |
| `scroll_up_down` の `tap-ms` | **45ms** | 45ms の release で2発目（60ms）をキャンセル。前後15msずつ余裕 |
| `MOVE_Y` | **±100** | 1イベントの value = 100 × 30/1000 = **3**（= 普通のマウスのノッチ3個分） |
| `triggers-per-rotation` | **12** | 境界が 30度 = 1ノッチ。10（=36度）だと 30度 と噛み合わずムラになる |

これで **1ノッチ → 1トリガ → 1 press/release → 1 tick → 1 ホイールイベント** と
一本道になる。

> **触るときの注意**: 量を変えたいときは `MOVE_Y` の値だけを増減すること。
> `tap-ms` と `trigger-period-ms` の比を崩すと 1ノッチのイベント数が変わり、
> YouTube 等の「イベント数で量が決まる」UI でまた倍になる。

コミット: `7577af1`, `ef9ad05`

---

#### 5. タイピング振動でオートマウスレイヤー（AML）が暴発する → 改善

2点で対処した。

**`require-prior-idle-ms` を 500 → 1000**

`temp_layer` はイベントをそのまま `CONTINUE` するため、このガードが止めるのは
**レイヤー切替だけでカーソル移動は素通りする**。つまり打鍵直後もボールでカーソルは
動き、おあずけになるのはクリック層だけなので、伸ばしても実害がほとんどない。

**`excluded-positions` から 0 / 10 / 22（Q / A / X）を外す**

このプロパティは ZMK の実装（`handle_position_state_changed`）上
**「押しても AML を解除しないキー」**であって、「押している間 AML へ移行しない
キー」ではない。除外されていないキーを押すと AML は即解除される。

したがって除外すべきは MOUSE 層で実際に機能を持つ 18/19/20（J K L）と 30/32（M .）
だけ。moNa2 から一緒に移ってきた Q / A / X は MOUSE 層で `&trans` なので守る必要が
なく、誤発火した AML を次の打鍵で解除する機会を潰していた（特に A は頻出）。

なお ZMK / cormoran 版ドライバ側に**移動量のしきい値に相当する設定は無い**
（`zip_temp_layer` のプロパティは上記2つのみ、`scaler` も 0 になった値を STOP せず
素通しする）。これ以上絞るなら AML タイムアウト短縮か cpi を下げる方向になる。

コミット: `94ac97c`

---

#### 6. DYA Studio / ZMK Studio に接続できない → 原因判明

**BLE 経路**: `CONFIG_ZMK_STUDIO_LOCKING=n` が原因だった。

```
config ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK
    default y if ZMK_STUDIO_LOCKING && ZMK_BLE
    help ... It's required to detect device from web bluetooth API for some browsers.
```

`LOCKING=n` だとこの directed advertising が無効になり、`&studio_unlock` を押しても
**Web Bluetooth のデバイス選択リストに出てこない**。本家 roBa の `n` をそのまま
継承していたのが誤りで、moNa2 と同じ `y` に揃えた。

> `&studio_unlock` 自体は `ZMK_STUDIO_LOCKING` ではなく `ZMK_STUDIO` に依存するので
> `LOCKING=n` でもビルドはされる。**behavior が存在することと機能することは別**。

**USB 経路**: Mac の有線では問題なく接続できたため、**ファーム側は正常**。
Windows 機固有の問題として切り離した（下記「既知の制約」参照）。

コミット: `78f23a0`

---

### 未解決・既知の制約

#### エンコーダ: 回し始めの1ノッチが空振りすることがある

回し続けている間は出ない。`ec11_trigger.c` は割り込みコールバックの冒頭で
`setup_int(dev, false)` と A/B 両方の割り込みを止め、実処理が終わってから戻す。
この**目隠し区間**に2遷移入ると `ec11_sample_fetch` のデコード表が
`default: delta = 0` で黙って捨てるため、**2パルス（15度 = 半ノッチ）が消える**。

15度ずれると累積が 15 → 45 → 45 … と推移するので、1ノッチ目だけトリガが出ず、
以降は毎ノッチ1トリガで安定する。症状と一致する。

**試して効かなかった手**: 「目隠し区間がシステムワークキューの待ちで伸びている」と
読んで `CONFIG_EC11_TRIGGER_OWN_THREAD=y`（専用スレッド）を実機で試したが、症状は
変わらなかった。**この読みは外れている**ので再試行しないこと（`521d0a6` で revert 済み）。

根治にはドライバをフォークしてデコード表を2ステップ遷移対応にする必要がある。

#### Windows 機で Studio に接続できない

| 環境 | 結果 |
|---|---|
| Mac + USB（有線） | 接続可 |
| Windows + USB | `Failed to get device information`（ポートは開くが RPC 無応答） |
| Windows + BT | デバイス選択に出ない（`LOCKING=n` が原因。修正済み） |

Mac で通る以上ファームの問題ではない。Windows 側を追うなら `usbser.sys` の割り当てや
デバイスマネージャーでの COM の状態から。

**編集は Mac から有線で行う運用が成立する。** BLE でも接続自体はできるが、物理
レイアウトや多層キーマップのような大きい RPC ペイロードが時間切れになるため、
キーマップ・コンボ・マクロの編集は有線でないと実用にならない。

#### USB シリアルポートが2本見える

`xiao_ble` ボード定義（Zephyr 4.1）が `boards/common/usb/cdc_acm_serial.dtsi` を
include しており、**ボード自身がコンソール用の CDC ACM を常に1本生やす**:

```dts
/ { chosen { zephyr,console = &board_cdc_acm_uart; ... }; };
&zephyr_udc0 {
    board_cdc_acm_uart: board_cdc_acm_uart { compatible = "zephyr,cdc-acm-uart"; };
};
```

そこへ `studio-rpc-usb-uart` スニペットが RPC 用をもう1本足すので、通常版でも
COM が2つ（例: `roBa（COM11）` と `roBa（COM15）`）現れる。**ログを有効にして
いなければボード側コンソールは何も喋らないため、2本とも同じ名前で無言**、
見分けがつかない。Studio でボード側を選ぶと当然失敗する。

消すなら `&board_cdc_acm_uart { status = "disabled"; };` ＋ `CONFIG_CONSOLE=n`
（cormoran 氏自身のモジュールのテスト設定と同じ手法。ログ版は
`-S zmk-usb-logging` が自前のコンソールを用意するので影響しない）。**未適用。**

---

### 調整ポイント早見表

| 変えたいもの | 場所 | 備考 |
|---|---|---|
| ホイールのスクロール量 | `config/roBa.keymap` の `MOVE_Y(±100)` | `tap-ms` / `trigger-period-ms` は触らない |
| ホイールのトリガ頻度 | `boards/shields/roBa/roBa.dtsi` の `steps` / `triggers-per-rotation` | 1ノッチ = 1トリガを維持すること |
| トラックボールのスクロール速度 | `roBa_R.overlay` の `scroller` の `zip_scroll_scaler` | Studio からも実行時調整可 |
| パン速度 | 同 `panner` の `zip_scroll_scaler` | |
| AML の起きにくさ | `roBa_R.overlay` の `require-prior-idle-ms` | 伸ばしてもカーソル移動には影響しない |
| AML のタイムアウト | 同 `&zip_temp_layer 7 500` の 500 | |
| カーソル速度 | `roBa_R.overlay` の `cpi` | 下げるとスクロール/パンも遅くなる |

---

### 診断用ビルド

`build.yaml` に `roBa_R_logging` を用意してある。`roBa_R` と同じ構成に
`zmk-usb-logging` スニペットを足したもので、ログ用の CDC ACM が別に生える。
「何が起きているか分からない」ときだけ使うこと（常用しない）。

再利用ワークフローの `snippet:` は `-S "..."` と1個しか渡せないため、2つ使うときは
`cmake-args` で `-DSNIPPET="a;b"` と渡している。
