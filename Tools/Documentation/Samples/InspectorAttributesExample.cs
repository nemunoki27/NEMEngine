using NEMEngine;

[DefaultExecutionOrder(-50)]
public sealed class InspectorAttributesExample : MonoBehaviour {

    // 保存対象へラベル・範囲・説明を組み合わせる
    [SerializeField, Label("移動速度"), Tooltip("1秒あたりの移動量")]
    [NEMEngine.Range(0.0f, 10.0f)]
    private float speed = 2.0f;

    [SerializeField, Min(0.0f), DragSpeed(0.05f)]
    private float power = 1.0f;

    [SerializeField, ReadOnly]
    private int hitCount = 0;

    // 非表示の保存値と、保存しない一時値を分ける
    [HideInInspector] public int savedCount;
    [System.NonSerialized] public int temporaryCount;

    [SerializeField, Multiline]
    private string description = "動作の説明";

    [SerializeField, SeparatorText("参照")]
    private GameObject? target = null;

    [SerializeField, FormerlySerializedAs("oldDuration")]
    private float duration = 1.0f;

    // 固定した設定型と、選んだ派生型を持つ設定を用意
    [SerializeField] private MovementOptions options = new MovementOptions();
    [SerializeReference] private IExampleAction? action = new JumpAction();
}

[System.Serializable]
public sealed class MovementOptions {
    public float duration = 1.0f;
}

public interface IExampleAction {
}

[System.Serializable]
public sealed class JumpAction : IExampleAction {
    public float height = 1.0f;
}
