using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	InspectorShowcase
//	Inspector の各 field 型と属性を 1 つの Script に集約した showcase
//============================================================================
public sealed class InspectorShowcase : ScriptBehaviour
{
    [Header("Primitives")]
    [SerializeField] private bool toggle = true;
    [SerializeField] private int count = 3;
    [Range(0.0f, 1.0f)]
    [SerializeField] private float ratio = 0.5f;
    [Min(0.0f)]
    [DragSpeed(0.1f)]
    [SerializeField] private float speed = 1.0f;
    [Tooltip("表示名などの文字列")]
    [SerializeField] private string label = "showcase";
    [Multiline]
    [SerializeField] private string notes = "line1\nline2";

    [Header("Math")]
    [SerializeField] private Vector3 offset = new(0.0f, 1.0f, 0.0f);
    [SerializeField] private Color4 tint = new(1.0f, 1.0f, 1.0f, 1.0f);

    [Header("References")]
    [SerializeField] private EntityRef target;
    [SerializeField] private AssetRef<TextureAsset> texture;

    [Header("List")]
    [SerializeField] private List<int> weights = new() { 1, 2, 3 };

    // Inspector に出さない runtime 専用フィールド
    [HideInInspector]
    [SerializeField] private float hiddenValue = 42.0f;

    // 表示はするが編集不可
    [ReadOnly]
    [SerializeField] private int readOnlyCounter;

    public override void Start()
    {
        readOnlyCounter = count + weights.Count;
        VerticalSliceBootstrap.Report(
            $"Inspector.Start toggle={toggle} count={count} ratio={ratio} speed={speed} " +
            $"label={label} offset={offset} tint={tint} weights={weights.Count} hidden={hiddenValue} readonly={readOnlyCounter} " +
            $"targetValid={target.isValid} textureAssigned={texture.isValid}");
    }
}
