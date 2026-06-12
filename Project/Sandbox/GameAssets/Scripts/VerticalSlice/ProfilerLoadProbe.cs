using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	ProfilerLoadProbe
//	Script Profiler の callback 別集計を確認するための bounded な callback 負荷 probe
//============================================================================
public sealed class ProfilerLoadProbe : ScriptBehaviour
{
    [Tooltip("各 callback で回す軽量計算の反復回数（極端な値を入れない）")]
    [Min(0.0f)]
    [SerializeField] private int workIterations = 200;

    // 計算結果を最適化で消されないよう保持する（log はしない）
    private float accumulator;

    public override void Update()
    {
        accumulator += DoWork();
    }

    public override void FixedUpdate()
    {
        accumulator += DoWork();
    }

    public override void LateUpdate()
    {
        accumulator += DoWork();
    }

    private float DoWork()
    {
        float sum = 0.0f;
        for (int i = 0; i < workIterations; ++i) {
            sum += MathF.Sqrt(i + 1);
        }
        return sum;
    }
}
