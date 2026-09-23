/* ホストで実行: cc -Wall -Wextra -Werror -I drivers/sensor/detent_encoder \
 *   tests/detent_decoder_test.c -o /tmp/t && /tmp/t
 * 実機に焼く前に、パルス欠落時の挙動を ec11 と比べて確認するためのテスト。 */
#include <stdio.h>
#include <stdlib.h>
#include "detent_decoder.h"

/* 正方向のグレイコード列 (ec11 と同じ符号) */
static const uint8_t FWD[4] = {0b00, 0b01, 0b11, 0b10};
static int failures;
static int phys; /* 実際のつまみ位置 (FWD の添字)。デコーダの内部状態とは別 */

static int idx_of(uint8_t ab) {
    for (int i = 0; i < 4; i++)
        if (FWD[i] == ab)
            return i;
    abort();
}

/* rest から dir 方向に1ノッチ (4遷移) 回す。drop のビットが立った遷移は
 * 割り込み取りこぼしとしてデコーダに渡さない。戻り値は確定ノッチ数。 */
static int notch(struct detent_decoder *d, int dir, unsigned drop) {
    int got = 0;
    for (int i = 0; i < 4; i++) {
        phys = (phys + dir + 4) % 4;
        if (!(drop & (1u << i)))
            got += detent_decoder_feed(d, FWD[phys]);
    }
    return got;
}
static void start(struct detent_decoder *d, uint8_t rest) {
    detent_decoder_init(d, rest);
    phys = idx_of(rest);
}

#define EXPECT(cond, ...)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* 比較用: ZMK 純正 ec11 + sensor-rotate (steps=48, 30度ごと) の再現 */
struct legacy { uint8_t state; int deg2; /* 0.5度単位の端数 */ };
static int legacy_feed(struct legacy *l, uint8_t ab) {
    int delta = 0;
    switch ((l->state << 2) | ab) {
    case 0b0001: case 0b0111: case 0b1110: case 0b1000: delta = 1; break;
    case 0b0010: case 0b0100: case 0b1101: case 0b1011: delta = -1; break;
    }
    l->state = ab;
    l->deg2 += delta * 15; /* 7.5度 */
    int t = l->deg2 / 60;  /* 30度 */
    l->deg2 %= 60;
    return t;
}
static int legacy_notch(struct legacy *l, int dir, unsigned drop) {
    int got = 0;
    for (int i = 0; i < 4; i++) {
        phys = (phys + dir + 4) % 4;
        if (!(drop & (1u << i)))
            got += legacy_feed(l, FWD[phys]);
    }
    return got;
}

int main(void) {
    struct detent_decoder d;

    /* 1. 通常回転: 1ノッチ = 1、両方向、静止位置は4種どれでも */
    for (int r = 0; r < 4; r++) {
        start(&d, FWD[r]);
        for (int k = 0; k < 20; k++)
            EXPECT(notch(&d, +1, 0) == 1, "fwd rest=%d k=%d", r, k);
        for (int k = 0; k < 20; k++)
            EXPECT(notch(&d, -1, 0) == -1, "rev rest=%d k=%d", r, k);
    }

    /* 2. 途中の1遷移を取りこぼしても (A/B 同時変化に見える) 1ノッチになり、
     *    位相もずれない。新ドライバは割り込みを止めないので、現実に起きうる
     *    欠落はこの1遷移分。連続2遷移の欠落は「3つ進んだ」と「1つ戻った」が
     *    区別できず、原理的にどのデコーダでも復元できない。 */
    for (int r = 0; r < 4; r++) {
        for (int dir = -1; dir <= 1; dir += 2) {
            for (int i = 0; i < 3; i++) {
                start(&d, FWD[r]);
                EXPECT(notch(&d, dir, 1u << i) == dir, "rest=%d dir=%d drop#%d", r, dir, i);
                EXPECT(d.acc == 0 && d.state == d.rest, "phase rest=%d dir=%d drop#%d", r, dir, i);
                EXPECT(notch(&d, dir, 0) == dir, "next rest=%d dir=%d drop#%d", r, dir, i);
                EXPECT(notch(&d, -dir, 0) == -dir, "reverse rest=%d dir=%d drop#%d", r, dir, i);
            }
        }
    }

    /* 3. 最後の遷移 (静止位置へ戻る) を落とした場合: ドライバは最後の
     *    エッジから 20ms 後にピンを読み直して feed する。それで確定する */
    for (int dir = -1; dir <= 1; dir += 2) {
        start(&d, 0b11);
        int n = notch(&d, dir, 0b1000);
        n += detent_decoder_feed(&d, FWD[phys]); /* 20ms 後の読み直し */
        EXPECT(n == dir, "drop-last+idle dir=%d -> %d", dir, n);
        EXPECT(notch(&d, dir, 0) == dir, "drop-last then dir=%d", dir);
        /* 読み直す前に次のノッチが来ても、合計は失われない */
        start(&d, 0b11);
        n = notch(&d, dir, 0b1000) + notch(&d, dir, 0);
        EXPECT(n == 2 * dir, "drop-last no-idle dir=%d -> %d", dir, n);
    }

    /* 4. 反転: 欠落の後に逆回転しても空振りしない */
    start(&d, 0b11);
    EXPECT(notch(&d, +1, 0b0010) == 1, "drop then fwd");
    EXPECT(notch(&d, -1, 0) == -1, "reverse after drop");
    EXPECT(notch(&d, +1, 0) == 1, "re-reverse");

    /* 5. 行って戻る / 接点のばたつきはノッチにならない */
    start(&d, 0b11);
    int pos = phys, got = 0;
    int wiggle[] = {+1, -1, +1, +1, -1, -1};
    for (unsigned i = 0; i < sizeof(wiggle) / sizeof(*wiggle); i++) {
        pos = (pos + wiggle[i] + 4) % 4;
        got += detent_decoder_feed(&d, FWD[pos]);
    }
    EXPECT(got == 0 && d.acc == 0, "wiggle -> %d", got);

    /* 比較: 純正方式では同じ欠落パターンで空振りする (テストの前提確認) */
    struct legacy l = {0b11, 0};
    phys = idx_of(0b11);
    int lost = 0;
    lost += legacy_notch(&l, +1, 0b0010) == 0; /* 1遷移欠落のノッチ */
    lost += legacy_notch(&l, -1, 0) == 0;      /* 直後の逆回転 */
    printf("legacy ec11 missed %d of 2 notches in scenario 4\n", lost);
    EXPECT(lost > 0, "legacy model should reproduce the miss");

    printf(failures ? "%d FAILURE(S)\n" : "all passed\n", failures);
    return failures != 0;
}
