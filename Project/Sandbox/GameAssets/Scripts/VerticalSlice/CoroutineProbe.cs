using System.Collections;
using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	CoroutineProbe
//	coroutine の start と周期 yield と owner 破棄時の自動 cancel を確認する probe
//============================================================================
public sealed class CoroutineProbe : ScriptBehaviour
{
    [Tooltip("tick 間隔（秒）")]
    [SerializeField] private float intervalSeconds = 0.5f;

    // 無制限に記録しないための上限
    [SerializeField] private int maxTicks = 8;

    private int tickCount;

    public override void Start()
    {
        tickCount = 0;
        StartCoroutine(TickRoutine());
        VerticalSliceBootstrap.Report("Coroutine.Start scheduled");
    }

    private IEnumerator TickRoutine()
    {
        // 上限まで周期実行する。owner 破棄時は engine 側が coroutine を停止するため無限ループでも安全。
        while (tickCount < maxTicks) {
            yield return new WaitForSeconds(intervalSeconds);
            ++tickCount;
            VerticalSliceBootstrap.Report($"Coroutine.tick {tickCount}/{maxTicks}");
        }
    }
}
