/*
 * jis.h - JIS(日本語)配列OS向け 記号エイリアス
 *
 * 前提: OS側のキーボード配列が「JIS」に設定されていること。
 * ZMKはUS(ANSI)基準のHIDコードを送るため、JIS-OSでは記号位置がずれる。
 * このヘッダは「JIS-OSで意図した文字が出る」キーコードに別名を付ける。
 *
 * 出典: QMK quantum/keymap_extras/keymap_japanese.h を検証して移植。
 * 記号配置の解説: docs/keymap-phase1.md の「SYM（記号）」節を参照。
 */

#pragma once

// 単キー(シフト無し)
#define JIS_AT      LBKT             // @
#define JIS_LBKT    RBKT             // [
#define JIS_RBKT    NON_US_HASH      // ]
#define JIS_COLON   SQT              // :
#define JIS_SEMI    SEMI             // ;
#define JIS_CARET   EQUAL            // ^
#define JIS_MINUS   MINUS            // -   (長音「ー」もこのキー)
#define JIS_BSLH    INT1             // \   (ろ)
#define JIS_YEN     INT3             // ¥
#define JIS_FSLH    FSLH             // /

// シフト付き
#define JIS_GRV     LS(LBKT)         // `
#define JIS_LBRC    LS(RBKT)         // {
#define JIS_RBRC    LS(NON_US_HASH)  // }
#define JIS_STAR    LS(SQT)          // *
#define JIS_PLUS    LS(SEMI)         // +
#define JIS_TILDE   LS(EQUAL)        // ~
#define JIS_EQUAL   LS(MINUS)        // =
#define JIS_UNDER   LS(INT1)         // _
#define JIS_PIPE    LS(INT3)         // |
#define JIS_EXCL    LS(N1)           // !
#define JIS_DQT     LS(N2)           // "
#define JIS_HASH    LS(N3)           // #
#define JIS_DLR     LS(N4)           // $
#define JIS_PCT     LS(N5)           // %
#define JIS_AMPS    LS(N6)           // &
#define JIS_QUOTE   LS(N7)           // '
#define JIS_LPAR    LS(N8)           // (
#define JIS_RPAR    LS(N9)           // )

// US配列と同じ位置のもの(利便のため別名を用意)
#define JIS_LABK    LS(COMMA)        // <
#define JIS_RABK    LS(DOT)          // >
#define JIS_QUES    LS(FSLH)         // ?

// JIS固有キー
#define JIS_HENKAN  INT4             // 変換
#define JIS_MHENKAN INT5             // 無変換
#define JIS_KANA    INT2             // かな
