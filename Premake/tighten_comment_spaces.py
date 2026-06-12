#!/usr/bin/env python3
# templateClass ルールの「コメント内の文字の間にスペースを入れない」を機械適用する補助
# 純粋なコメント行のみ対象にし、日本語文字に隣接する半角スペースだけを除去する
# 英単語どうしの間のスペース GPU Hang などは両側 ASCII なので保持される
# 。/（） の言い換えは意味保持が要るためこのスクリプトでは触らない

import sys
import re
import io

# Hiragana Katakana CJK と全角記号の範囲を日本語文字とみなす
JP = (
    "぀-ヿ"   # かな
    "㐀-䶿"   # CJK 拡張A
    "一-鿿"   # CJK 統合漢字
    "＀-￯"   # 全角英数記号
    "　-〿"   # CJK 記号と句読点
)

# 日本語文字に隣接するスペースだけを落とす、両側 ASCII のスペースは残す
RE_BEFORE = re.compile(r"([" + JP + r"]) +")
RE_AFTER = re.compile(r" +([" + JP + r"])")


def tighten_comment_text(text):
    prev = None
    cur = text
    # 連続適用で日本語の間の複数スペースも収束させる
    while cur != prev:
        prev = cur
        cur = RE_BEFORE.sub(r"\1", cur)
        cur = RE_AFTER.sub(r"\1", cur)
    return cur


def process_line(line):
    # 行頭空白 + // で始まる純粋なコメント行のみ対象
    m = re.match(r"^(\s*//)(.*)$", line.rstrip("\n"))
    if not m:
        return line, False
    head, body = m.group(1), m.group(2)
    # box 罫線や divider 行は触らない
    if "====" in line or set(body.strip()) <= set("-"):
        return line, False
    # 文字列リテラルらしき "..." を含む行はスキップして安全側に倒す
    if '"' in body:
        return line, False
    new_body = tighten_comment_text(body)
    if new_body == body:
        return line, False
    newline = "\n" if line.endswith("\n") else ""
    return head + new_body + newline, True


def main():
    args = sys.argv[1:]
    dry = "--dry" in args
    files = [a for a in args if not a.startswith("--")]
    total_changed = 0
    changed_files = 0
    samples = []
    for path in files:
        with io.open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        out = []
        file_changed = 0
        for ln in lines:
            new, changed = process_line(ln)
            if changed:
                file_changed += 1
                if len(samples) < 40:
                    samples.append((path, ln.rstrip("\n"), new.rstrip("\n")))
            out.append(new)
        if file_changed:
            changed_files += 1
            total_changed += file_changed
            if not dry:
                with io.open(path, "w", encoding="utf-8", newline="") as f:
                    f.writelines(out)
    print("files changed: %d  lines changed: %d  (dry=%s)" % (changed_files, total_changed, dry))
    for p, b, a in samples:
        print("---", p)
        print("  -", b)
        print("  +", a)


if __name__ == "__main__":
    main()
