namespace NEMEngine;

// 世代と実行値を保持するスクリプト格納枠
internal sealed class ScriptInstanceSlot {

    // 1始まりの世代で0は無効参照
    internal uint generation;
    internal MonoBehaviour? instance;
    internal bool inUse;
    // 世代が枯渇した枠は再利用しない
    internal bool retired;
    // サイズ取得とコピーで共有する実行時データ
    internal byte[]? runtimeStateSnapshot;
    // 複製用の保存値をサイズ取得からコピーまで保持する
    internal byte[]? savedStateSnapshot;
    // Hot Reload用のprivate値をサイズ取得からコピーまで保持する
    internal byte[]? reloadStateSnapshot;
}
