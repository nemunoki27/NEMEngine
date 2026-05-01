# NEMEngine Visual Studio Debug Settings

## 目的

NEMEngine では C# スクリプトの変更反映を Visual Studio Hot Reload ではなく、エンジン側の `Unload -> Build -> Reload` で行います。

Visual Studio は native + managed の混合デバッグ、C# ブレークポイント解決、値確認、Step、Continue に集中させます。IDE 側 Hot Reload が有効なままだと、mixed-mode デバッグ中の Continue 時に Visual Studio が別経路で変更適用を試み、「Hot Reload の変更はサポートされていません」系のダイアログが出ることがあります。

このため、NEMEngine では Visual Studio の Hot Reload 無効化を推奨します。

## 使い方

1. Visual Studio を閉じます。
2. `Tools/VisualStudio/apply_vs_debug_settings.bat` を実行します。
3. Visual Studio を開き直します。
4. `Project/NEMEngine.slnx` を開き、`Sandbox` を Startup Project にします。
5. F5 で mixed native/managed debug を開始します。
6. Edit 中に `.cs` を保存した場合、変更反映はエンジン側の自動ビルドと再ロードに任せます。
7. Play 後に C# ブレークポイントで停止し、Continue 時に Hot Reload ダイアログが出ないことを確認します。

既定のスクリプト動作は import です。IDE 全体設定への影響が大きい `/ResetSettings` は既定では使いません。必要な場合だけ、明示的に次を実行します。

設定を変更せず、Visual Studio と設定ファイルを見つけられるかだけ確認する場合は次を実行します。

```bat
Tools\VisualStudio\apply_vs_debug_settings.bat --check
```

```bat
Tools\VisualStudio\apply_vs_debug_settings.bat --reset
```

## 手動確認

bat でうまく適用できない場合は、Visual Studio で次を確認します。

- `Tools > Options > Debugging > .NET/C++ Hot Reload`
- `Enable Hot Reload` をオフ
- `Enable Hot Reload on File Save` もオフ推奨

## トラブルシュート

### Continue 時にまだ Hot Reload ダイアログが出る

- Visual Studio を完全に再起動したか確認します。
- 設定適用後の新しいデバッグ セッションで確認します。
- `Tools > Options > Debugging > .NET/C++ Hot Reload` で Hot Reload が再度有効化されていないか確認します。

### C# ブレークポイントが白抜きになる

これは Hot Reload とは別問題です。まず以下を確認します。

- `Sandbox.vcxproj.user` の `LocalDebuggerDebuggerType` が `NativeWithManagedCore`
- `GameScripts.dll` と `GameScripts.pdb` の構成が一致している
- `Managed/$(Configuration)` が build / copy / runtime load で一貫して使われている

## 運用方針

NEMEngine の C# スクリプト運用では、IDE の Hot Reload ではなくエンジン側の再ロードを使います。Visual Studio の Hot Reload を再度有効にすると、mixed native/managed デバッグ時の Continue で IDE 側の変更適用が割り込み、エンジン側の再ロード機構と競合する可能性があります。
