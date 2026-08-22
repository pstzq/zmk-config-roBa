#!/usr/bin/env python3
"""keymap-drawer が出力した YAML を、図として読みやすくなるよう加工する。

やること2つ:

1. **コンボの復元**
   コンボを `zmk,combos` から zmk-feature-runtime-combo の
   `cormoran,runtime-combo-defaults` へ移行した結果、keymap-drawer の ZMK パーサが
   コンボを認識できなくなり、生成される図からコンボ表示が丸ごと落ちた。
   同ノードを読んで `combos:` セクションを復元する。

2. **レイヤー見出しに役割と入り方を併記**
   見出しは YAML のレイヤー名そのものが使われる（keymap-drawer の
   `layer_legend_map` はキー内の小さな層ラベル用で、見出しには効かない）。
   ファーム側の `display-name` は ZMK Studio にも出るので短いままにしたいため、
   図の中でだけ長い名前に差し替える。

使い方:
  python scripts/drawer_enrich.py config/roBa.keymap keymap-drawer/roBa.yaml
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# &kp LS(TAB) のような修飾関数を図向けの短い表記へ
MODIFIERS = {
    "LS": "Sft", "RS": "Sft",
    "LC": "Ctl", "RC": "Ctl",
    "LA": "Alt", "RA": "Alt",
    "LG": "Gui", "RG": "Gui",
}

# レイヤー番号 -> 表示名。keymap の #define から拾うので手で同期する必要はない
def parse_layer_names(keymap: str) -> dict[str, str]:
    names: dict[str, str] = {}
    for m in re.finditer(r"^#define\s+(\w+)\s+(\d+)", keymap, re.MULTILINE):
        names[m.group(1)] = m.group(1)
    return names


def format_binding(binding: str) -> str | dict[str, str]:
    """DTS の bindings を keymap-drawer の `k:` 値へ変換する。

    変換できない形は素通しする（図には出るので、崩れても気づける）。
    """
    b = binding.strip().lstrip("&").strip()
    parts = b.split()
    if not parts:
        return binding.strip()

    behavior, args = parts[0], parts[1:]

    def kc(code: str) -> str:
        # LS(TAB) -> Sft+TAB  / ネストは1段だけ想定
        m = re.fullmatch(r"(\w+)\((.+)\)", code)
        if m and m.group(1) in MODIFIERS:
            return f"{MODIFIERS[m.group(1)]}+{kc(m.group(2))}"
        return code

    if behavior == "kp" and len(args) == 1:
        return kc(args[0])
    if behavior == "lt" and len(args) == 2:
        # tap = キー / hold = レイヤー
        return {"t": kc(args[1]), "h": args[0]}
    if behavior == "mo" and len(args) == 1:
        return {"h": args[0]}
    if behavior == "to" and len(args) == 1:
        return {"t": args[0]}
    if behavior == "tog" and len(args) == 1:
        return {"t": args[0], "h": "toggle"}
    if behavior == "mkp" and len(args) == 1:
        return args[0]
    return b


def parse_runtime_combos(keymap: str) -> list[dict]:
    """runtime_combo_defaults ノードの子を順に読む。"""
    node = re.search(
        r"runtime_combo_defaults\s*\{(.*?)\n\s{4}\};", keymap, re.DOTALL
    )
    if not node:
        return []
    body = node.group(1)

    combos: list[dict] = []
    # 子ノード: 名前 { ... };
    for child in re.finditer(r"(\w+)\s*\{(.*?)\}\s*;", body, re.DOTALL):
        props = child.group(2)

        pos = re.search(r"key-positions\s*=\s*<([^>]*)>", props)
        binds = re.search(r"bindings\s*=\s*<([^>]*)>", props)
        if not pos or not binds:
            continue

        entry: dict = {
            "p": [int(x) for x in pos.group(1).split()],
            "k": format_binding(binds.group(1)),
        }

        layers = re.search(r"layers\s*=\s*<([^>]*)>", props)
        if layers:
            entry["l"] = layers.group(1).split()

        # display-name があれば図の補足として持たせる
        name = re.search(r'display-name\s*=\s*"([^"]*)"', props)
        if name:
            entry["_name"] = name.group(1)

        combos.append(entry)
    return combos


def to_yaml(combos: list[dict]) -> str:
    """依存を増やしたくないので、必要な範囲だけ手で整形する。"""
    lines = ["combos:"]
    for c in combos:
        lines.append(f"- p: [{', '.join(str(p) for p in c['p'])}]")
        k = c["k"]
        if isinstance(k, dict):
            inner = ", ".join(f"{key}: {val}" for key, val in k.items())
            lines.append(f"  k: {{{inner}}}")
        else:
            lines.append(f"  k: {k}")
        if "l" in c:
            lines.append(f"  l: [{', '.join(c['l'])}]")
    return "\n".join(lines) + "\n"


# 図の見出し用の長い名前。キーは keymap-drawer が出す層名
# （= config/roBa.keymap の display-name）。層を増やしたらここにも足すこと。
# 未登録の層はそのままの名前で描かれるので、抜けても図は壊れない。
LAYER_HEADINGS = {
    "MAC":    "0 · MAC — ベース (Mac)",
    "WIN":    "1 · WIN — ベース (Windows)",
    "O24":    "2 · O24 — 代替配列 · Q+P でトグル",
    "SYM":    "3 · SYM — 記号 · Space 長押し",
    "NUM":    "4 · NUM — 数字 · Enter 長押し",
    "NAV-M":  "5 · NAV-M — 矢印/ナビ (Mac) · 英数 or かな 長押し",
    "NAV-W":  "6 · NAV-W — 矢印/ナビ (Win) · 英数 or かな 長押し",
    "MOUSE":  "7 · MOUSE — 自動マウス層 · ボールを動かすと自動 / 500ms 静止で復帰",
    "BLE":    "8 · BLE — 接続と設定 · 英数+かな",
    "PAN":    "9 · PAN — パン (2Dスクロール) · P 長押し + ボール",
    "SNAP-M": "10 · SNAP-M — ウィンドウ整列 (Mac/Rectangle) · Q 長押し + ボール",
    "SNAP-W": "11 · SNAP-W — ウィンドウ整列 (Win) · Q 長押し + ボール",
    "CURSOR": "12 · CURSOR — 予約 (未使用)",
    "CLICK":  "13 · CLICK — クリック層 · Del or caps 長押し",
}


def rename_layer_headings(doc: str) -> tuple[str, int]:
    """`layers:` 直下のキー名を LAYER_HEADINGS に従って差し替える。

    YAML を読み書きし直すと keymap-drawer 特有の書式（`k: {t: .., h: ..}` の
    フロースタイル等）が崩れるので、行単位で置換する。

    注意: コンボの `l: [WIN, MAC]` も同じ名前を参照しており、keymap-drawer は
    「コンボの層名が層定義に無い」場合バリデーションエラーで落ちる。
    見出しを変えるならこちらも必ず一緒に変える。
    """
    out, renamed, in_layers = [], 0, False
    for line in doc.splitlines():
        # コンボの層参照。layers: セクションの外にあるのでここで先に処理する
        m_l = re.match(r"^(\s*l:\s*)\[(.*)\]\s*$", line)
        if m_l:
            names = [n.strip() for n in m_l.group(2).split(",") if n.strip()]
            mapped = [f'"{LAYER_HEADINGS[n]}"' if n in LAYER_HEADINGS else n for n in names]
            out.append(f"{m_l.group(1)}[{', '.join(mapped)}]")
            continue

        if re.match(r"^layers:\s*$", line):
            in_layers = True
            out.append(line)
            continue
        if in_layers:
            # インデント2のキーがレイヤー名。1桁インデントの別セクションが来たら抜ける
            m = re.match(r"^  ([^\s:][^:]*):\s*$", line)
            if m and m.group(1) in LAYER_HEADINGS:
                out.append(f'  "{LAYER_HEADINGS[m.group(1)]}":')
                renamed += 1
                continue
            if re.match(r"^\S", line):
                in_layers = False
        out.append(line)
    return "\n".join(out) + "\n", renamed


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    keymap_path, yaml_path = Path(sys.argv[1]), Path(sys.argv[2])
    keymap = keymap_path.read_text(encoding="utf-8")
    combos = parse_runtime_combos(keymap)

    if not combos:
        print(f"警告: {keymap_path} に runtime_combo_defaults が見つからない。何もしない")
        return 0

    doc = yaml_path.read_text(encoding="utf-8")
    # 既に combos: があれば入れ替える
    doc = re.sub(r"^combos:\n(?:[-\s].*\n)*", "", doc, flags=re.MULTILINE)

    # layers: の直前へ差し込む（keymap-drawer の出力順に合わせる）
    if "\nlayers:" in doc:
        doc = doc.replace("\nlayers:", "\n" + to_yaml(combos) + "layers:", 1)
    else:
        doc = doc.rstrip() + "\n" + to_yaml(combos)

    doc, renamed = rename_layer_headings(doc)

    yaml_path.write_text(doc, encoding="utf-8")
    print(f"{yaml_path}: コンボ {len(combos)} 件を注入 / 見出し {renamed} 件を差し替え")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
