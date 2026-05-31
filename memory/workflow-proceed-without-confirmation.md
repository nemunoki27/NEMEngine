---
name: workflow-proceed-without-confirmation
description: User wants autonomous execution without per-step confirmation prompts
metadata:
  type: feedback
---

ビルドやコマンド実行のたびに「実行してよいか」を確認しない。承認を求めず、どんどん実装を進めてよい。

**Why:** ユーザーが明示的に「Proceedでいちいち聞かなくていい、どんどん進めて」と指示した。
**How to apply:** 実装→ビルド→修正のループを自分で回しきり、完了したらまとめて報告する。Git操作など[[nemengine-build-workflow]]に無い破壊的操作だけは従来どおり確認する。
