namespace GameScripts;

// UnityLikeReferenceTestの参照先として使うスクリプト
// ScriptBehaviour参照フィールドの解決確認と、参照経由のメソッド呼び出し確認に使う
public class ReferenceTestTarget : ScriptBehaviour {

    // インスペクターから識別用に設定する
    public string label = "target";

    // 参照経由で呼ばれた回数
    private int pingCount;

    // 参照経由の呼び出し確認用
    public int Ping() {
        return ++pingCount;
    }
}
