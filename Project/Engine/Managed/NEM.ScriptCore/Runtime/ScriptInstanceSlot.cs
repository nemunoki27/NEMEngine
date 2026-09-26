namespace NEMEngine;

// 世代と実行値を保持するスクリプト格納枠
internal sealed class ScriptInstanceSlot {

    // 1始まりの世代。0は無効handleを表す
    internal uint generation;
    internal MonoBehaviour? instance;
    internal bool inUse;
    // generation枯渇でこの枠を永久欠番にした（再利用しない）
    internal bool retired;
    // サイズ取得とコピーで共有する実行時データ
    internal byte[]? runtimeStateSnapshot;
}
