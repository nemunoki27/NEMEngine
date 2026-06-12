using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	RuntimeApplyProbe
//	runtime で変化する serialized field を持ち runtime readback と Apply To Authoring を確認する probe
//============================================================================
public sealed class RuntimeApplyProbe : ScriptBehaviour
{
    [Tooltip("Play 中に変化する値。Apply Runtime Values To Authoring の対象")]
    [SerializeField] private float tunedValue = 0.0f;

    [Tooltip("runtime での増加レート")]
    [SerializeField] private float ratePerSecond = 1.0f;

    [SerializeField] private int applyTickCount;

    public override void Start()
    {
        VerticalSliceBootstrap.Report($"RuntimeApply.Start tunedValue={tunedValue}");
    }

    public override void Update()
    {
        // runtime 専用の変化（authoring とは別）。Apply するまで authoring には反映されない。
        tunedValue += ratePerSecond * Time.deltaTime;
        ++applyTickCount;
    }
}
