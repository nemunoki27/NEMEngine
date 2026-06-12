using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	OptionalCollisionProbe
//	既存 collision lifecycle が ScriptBehaviour.OnCollision callback へ届くことを確認する probe
//============================================================================
public sealed class OptionalCollisionProbe : ScriptBehaviour
{
    private int enterCount;

    public override void OnCollisionEnter(Collision collision)
    {
        ++enterCount;
        VerticalSliceBootstrap.Report($"Collision.Enter {enterCount} with={collision.entity.name}");
    }

    public override void OnCollisionExit(Collision collision)
    {
        VerticalSliceBootstrap.Report($"Collision.Exit with={collision.entity.name}");
    }
}

//============================================================================
//	OptionalTimeScaleProbe
//	Time.TimeScale facade の取得と設定を確認する probe
//============================================================================
public sealed class OptionalTimeScaleProbe : ScriptBehaviour
{
    [Range(0.0f, 2.0f)]
    [SerializeField] private float desiredTimeScale = 1.0f;

    [SerializeField] private bool applyOnStart;

    public override void Start()
    {
        if (applyOnStart) {
            Time.TimeScale = desiredTimeScale;
        }
        VerticalSliceBootstrap.Report($"TimeScale.Start scale={Time.TimeScale}");
    }
}

//============================================================================
//	OptionalExceptionProbe
//	例外診断の確認用で throwNow を true にした最初の Update でのみ意図的に throw する probe
//============================================================================
public sealed class OptionalExceptionProbe : ScriptBehaviour
{
    [Tooltip("true にした最初の Update で意図的に例外を出す（通常は false のまま）")]
    [SerializeField] private bool throwNow;

    private bool alreadyThrew;

    public override void Update()
    {
        if (!throwNow || alreadyThrew) {
            return;
        }
        alreadyThrew = true;
        // 構造化 exception 診断を確認するための意図的な例外
        throw new System.InvalidOperationException("OptionalExceptionProbe intentional fault");
    }
}
