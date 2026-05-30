---
name: nemengine-build-workflow
description: How to (re)generate VS projects and build the NEMEngine solution
metadata:
  type: project
---

NEMEngine の `.vcxproj` / `.vcxproj.filters` は **Premake 生成物**。手で編集せず、新規ソース追加後は `Premake\generate_vs2026.bat` を実行して再生成する（ソースツリーをグロブで拾うのでファイルを正しいフォルダに置けば自動で含まれる）。

ビルドは MSBuild (VS2026: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`)。
- エンジン単体: `MSBuild Project\Engine\NEMEngine.vcxproj /p:Configuration=Develop /p:Platform=x64`（静的 lib 出力）。
- 実行ファイル(Sandbox.exe)を含む全体: `MSBuild Project\NEMEngine.slnx /p:Configuration=Develop /p:Platform=x64`。
- **Sandbox.vcxproj を単体ビルドすると失敗する**: imgui/meshoptimizer/assimp/libcurl の外部プロジェクトは Debug/Release のみ定義で、Develop→Release のマッピングは **solution(.slnx) 側にしかない**。全体ビルドは必ず `.slnx` を対象にする。

構成は Debug / Develop / Release（いずれも x64）。
