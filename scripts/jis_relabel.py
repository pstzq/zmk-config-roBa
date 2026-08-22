#!/usr/bin/env python3
"""
keymap-drawer がパースした yaml のキーラベルを「JIS-OSで実際に出る文字」に
差し替えるスクリプト。

keymap-drawer は US(ANSI)基準でキーコードを描画するため、JIS運用だと記号が
`Sft+1` `[` `INT1` のように出て読めない。このスクリプトで JIS グリフへ変換する。

使い方:
    python jis_relabel.py <入力(US版yaml)> <出力(JIS版yaml)>
"""
import sys
import yaml

# keymap-drawer の US 表記 -> JIS で実際に出る文字
US_TO_JIS = {
    "Sft+1": "!", "Sft+2": '"', "Sft+3": "#", "Sft+4": "$", "Sft+5": "%",
    "Sft+6": "&", "Sft+7": "'", "Sft+8": "(", "Sft+9": ")",
    "[": "@", "]": "[", "#": "]",
    "Sft+]": "{", "Sft+#": "}",
    "=": "^", "Sft+=": "~", "Sft+-": "=", "Sft+;": "+",
    "Sft+[": "`", "'": ":", "Sft+'": "*",
    "INT1": "\\", "Sft+INT1": "_", "INT3": "¥", "Sft+INT3": "|",
    "Sft+,": "<", "Sft+.": ">", "Sft+/": "?",
}


def conv(item):
    if isinstance(item, str):
        return US_TO_JIS.get(item, item)
    if isinstance(item, dict) and isinstance(item.get("t"), str):
        item["t"] = US_TO_JIS.get(item["t"], item["t"])
    return item


def main():
    data = yaml.safe_load(open(sys.argv[1], encoding="utf-8"))
    data.pop("layout", None)  # 物理レイアウトは -j で外部指定する
    for name, keys in data.get("layers", {}).items():
        data["layers"][name] = [conv(k) for k in keys]
    yaml.safe_dump(data, open(sys.argv[2], "w", encoding="utf-8"),
                   allow_unicode=True, sort_keys=False)


if __name__ == "__main__":
    main()
