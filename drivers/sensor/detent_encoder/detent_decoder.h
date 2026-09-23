/*
 * ノッチ(デテント)同期型のロータリーエンコーダ・デコーダ。
 * ハードウェアに依存しない純粋なロジックなので tests/ からホストでも検証する。
 *
 * ZMK 純正 ec11 は「パルス数 x 角度」を積算し、sensor-rotate 側が
 * 30度ごとに切り捨て除算でトリガを出す。パルスを1つでも取りこぼすと
 * 端数がノッチ位置からずれたまま残り、以後「境界を跨がないノッチ」
 * = 空振りが出る (逆回転のたびにも1ノッチ落ちる)。
 *
 * ここでは代わりに「A/B が静止位置(ノッチ)の状態へ戻った瞬間」を1ノッチと
 * 数え、そこで積算をゼロに戻す (QMK の ENCODER_DEFAULT_POS と同じ考え方)。
 * 途中でパルスを落としても次のノッチで必ず位相が合い直すので、ずれが残らない。
 */
#pragma once

#include <stdint.h>

struct detent_decoder {
    uint8_t state;   /* 直前の A/B (A<<1 | B) */
    uint8_t rest;    /* ノッチで静止しているときの A/B */
    int8_t last_dir; /* 今のノッチ内で最後に確定した向き (+1/-1/0) */
    int16_t acc;     /* 静止位置を出てからの正味パルス数 */
};

static inline void detent_decoder_init(struct detent_decoder *d, uint8_t ab) {
    d->state = ab & 0x3;
    d->rest = ab & 0x3;
    d->last_dir = 0;
    d->acc = 0;
}

/*
 * 新しい A/B を1つ与え、確定したノッチ数 (符号付き) を返す。
 * 向きの符号は ZMK 純正 ec11 のデコード表と同じ (00->01->11->10 が正)。
 */
static inline int detent_decoder_feed(struct detent_decoder *d, uint8_t ab) {
    ab &= 0x3;
    if (ab == d->state) {
        return 0;
    }

    int8_t delta;
    switch ((d->state << 2) | ab) {
    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
        delta = 1;
        break;
    case 0b0010:
    case 0b0100:
    case 0b1101:
    case 0b1011:
        delta = -1;
        break;
    default:
        /* A と B が同時に変わった = 間の1遷移を取りこぼした。
         * ec11 はここを 0 として捨てていた。ノッチ内で向きが分かっていれば
         * その向きに2つ進んだとみなす。静止位置から直接跳んだ場合は向きが
         * 決められないので 0 のままにし、残りの遷移に任せる。 */
        delta = 2 * d->last_dir;
        break;
    }

    if (delta > 0) {
        d->last_dir = 1;
    } else if (delta < 0) {
        d->last_dir = -1;
    }
    if (d->acc > -1000 && d->acc < 1000) {
        d->acc += delta;
    }
    d->state = ab;

    if (ab != d->rest) {
        return 0;
    }

    /* 静止位置に戻った: 正味パルスを 4パルス=1ノッチ で四捨五入して確定。
     * 2パルス(半ノッチ)以上進んでいれば1ノッチと数えるので、途中で2つ
     * 落としても空振りしない。行って戻っただけ (acc=0) や接点のばたつき
     * (+1 -1) は 0 になる。 */
    int a = d->acc;
    int n = ((a < 0 ? -a : a) + 2) / 4;
    d->acc = 0;
    d->last_dir = 0;
    return a < 0 ? -n : n;
}

/* 静止位置を学び直す (起動時の読み取りが誤っていた場合の保険)。 */
static inline void detent_decoder_relearn(struct detent_decoder *d, uint8_t ab) {
    detent_decoder_init(d, ab);
}
