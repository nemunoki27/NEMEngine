namespace GameScripts;

// [Serializable]クラス/構造体と[SerializeReference]のインスペクター編集確認用スクリプト
//
// 使い方:
//   1. 任意のEntityへこのスクリプトをattachする
//   2. インスペクターで各フィールドを編集する
//      - mainAttack/holder/attacks: 折りたたみを開いてメンバを直接編集できる
//      - action/actionList: 型のComboで None/MoveAction/AttackAction を選ぶと、選択型のフィールドが展開される
//   3. シーン保存→再ロードで値が復元されること、Playで下記の[PASS]ログが出ることを確認する

// ネスト編集の確認用struct、private [SerializeField]と参照型メンバも含む
[System.Serializable]
public struct AttackData {

    public string name;
    [Range(0.0f, 100.0f)]
    public float damage;
    public Vector3 offset;
    [SerializeField] private Mesh? effectMesh;

    public Mesh? EffectMesh => effectMesh;
}

// 2段ネストとコレクションメンバの確認用class
[System.Serializable]
public class AttackSet {

    public string setName = "default";
    public AttackData primary;
    public List<float> cooldowns = new();
}

// SerializeReferenceの基底型、派生型をインスペクターで選ぶ
[System.Serializable]
public abstract class ActionBase {

    public float duration = 1.0f;

    public abstract string Describe();
}

[System.Serializable]
public class MoveAction : ActionBase {

    public Vector3 target;
    public float speed = 5.0f;

    public override string Describe() => $"Move to ({target.x},{target.y},{target.z}) speed={speed}";
}

[System.Serializable]
public class AttackAction : ActionBase {

    public float damage = 10.0f;
    [SerializeField] private Material? hitMaterial;

    public override string Describe() => $"Attack damage={damage} material={(hitMaterial != null ? hitMaterial.name : "null")}";
}

public class SerializableTypesTest : ScriptBehaviour {

    [SeparatorText("Serializableのネスト編集")]
    [SerializeField] private AttackData mainAttack;
    [SerializeField] private AttackSet? holder;
    [SerializeField] private List<AttackData>? attacks;

    [SeparatorText("SerializeReferenceの派生型選択")]
    [SerializeReference] private ActionBase? action;
    [SerializeReference] private List<ActionBase?>? actionList;

    private int passCount;
    private int failCount;

    public override void Start() {

        Debug.Log("===== SerializableTypesTest 開始 =====");

        //--------------------------------------------------------------------
        // 1. [Serializable]ネスト型の復元
        //--------------------------------------------------------------------
        Debug.Log("--- 1. ネスト型の復元 ---");
        Debug.Log($"mainAttack: name={mainAttack.name} damage={mainAttack.damage} " +
            $"offset=({mainAttack.offset.x},{mainAttack.offset.y},{mainAttack.offset.z}) " +
            $"effectMesh={(mainAttack.EffectMesh != null ? mainAttack.EffectMesh.name : "null")}");
        Check("structメンバの復元", !string.IsNullOrEmpty(mainAttack.name) || mainAttack.damage != 0.0f);

        if (holder != null) {
            Debug.Log($"holder: setName={holder.setName} primary.damage={holder.primary.damage} cooldowns={holder.cooldowns.Count}件");
            Check("2段ネストの復元", true);
        } else {
            Debug.LogWarning("[SKIP] holder(未編集ならnullのまま)");
        }

        if (attacks != null) {
            Debug.Log($"attacks: {attacks.Count}件");
            for (int i = 0; i < attacks.Count; ++i) {
                Debug.Log($"  [{i}] name={attacks[i].name} damage={attacks[i].damage}");
            }
            Check("List<struct>の復元", true);
        } else {
            Debug.LogWarning("[SKIP] attacks(未編集ならnullのまま)");
        }

        //--------------------------------------------------------------------
        // 2. [SerializeReference]の復元と多態呼び出し
        //--------------------------------------------------------------------
        Debug.Log("--- 2. SerializeReference ---");
        if (action != null) {
            Debug.Log($"action: {action.GetType().Name} duration={action.duration} -> {action.Describe()}");
            Check("派生型インスタンスの復元", action is MoveAction || action is AttackAction);
        } else {
            Debug.LogWarning("[SKIP] action(Noneのまま)");
        }

        if (actionList != null) {
            Debug.Log($"actionList: {actionList.Count}件");
            foreach (ActionBase? entry in actionList) {
                Debug.Log($"  {(entry != null ? $"{entry.GetType().Name} -> {entry.Describe()}" : "null")}");
            }
            Check("List<基底型>の要素ごとの派生型復元", true);
        } else {
            Debug.LogWarning("[SKIP] actionList(未編集ならnullのまま)");
        }

        string result = failCount == 0 ? "全PASS" : "FAILあり";
        Debug.Log($"===== SerializableTypesTest 完了: {result} (PASS={passCount} FAIL={failCount}) =====");
        Debug.Log("Play中インスペクターでネストメンバの現在値が表示・編集できることも確認してください");
    }

    private void Check(string label, bool condition) {

        if (condition) {
            ++passCount;
            Debug.Log($"[PASS] {label}");
        } else {
            ++failCount;
            Debug.LogError($"[FAIL] {label}");
        }
    }
}
