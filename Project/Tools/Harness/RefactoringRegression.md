# リファクタリング回帰確認

## 自動検証

通常の変更後は静的検証を実行する。

```powershell
pwsh -File Project/Tools/Harness/RefactoringBaseline.ps1 -Mode Verify -Scope Static
```

各Phase完了時は全検証を実行する。

```powershell
pwsh -File Project/Tools/Harness/RefactoringBaseline.ps1 -Mode Verify -Scope Full
```

基準値の更新は、改善内容と計測結果を確認した後に明示的に実行する。

```powershell
pwsh -File Project/Tools/Harness/RefactoringBaseline.ps1 -Mode Capture -Scope Full
```

計測結果は`Generated/Refactoring/BaselineReport.json`へ出力される。

## 自動検証範囲

- C++、C#、HLSLのファイル数と行数
- 小規模ファイル、巨大ファイル、完全重複ファイル
- CoreからEditorへのinclude
- Core内のImGui、MyGUI依存
- C++、HLSLのinclude表記
- Engine、SandboxのJSONパース
- Shader manifestのGUID参照
- Shader manifestに登録された全StageのDXCコンパイル
- C++とC#のManaged ABIテーブル
- Component Binding生成とAnalyzer
- Debug、Develop、ReleaseのSandboxビルド

## 手動確認

自動化されていない表示と操作は、各Phase完了時に次の順序で確認する。

1. DebugでEditorを起動し、SceneViewとGameViewが表示される
2. EditからPlay、Pause、FrameStep、Editへ戻れる
3. Sceneを保存して開き直し、HierarchyとComponentが一致する
4. Prefabを生成、編集、保存、再読込してEntityが増殖しない
5. Material、Shader、Textureを変更し、全Rendererへ反映される
6. Particle EffectとTrailを再生し、EditorとPlayで結果が一致する
7. C#のAwake、OnEnable、Start、Update、LateUpdate順序を確認する
8. Single、Additive、Unload、無効化、再有効化でC#ライフサイクルを確認する
9. AudioがPlay終了とScene遷移で停止する
10. Release出力を日本語を含むパスから起動し、SceneとAssetが読み込まれる

手動確認結果と未確認項目は、各Phaseの完了報告に記録する。
