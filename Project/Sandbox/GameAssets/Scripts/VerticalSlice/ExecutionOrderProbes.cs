using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	Execution Order probes
//	DefaultExecutionOrder の default と override と tie-break を deterministic に確認する probe 群
//============================================================================

// 早い順（負値）
[DefaultExecutionOrder(-100)]
public sealed class ExecutionOrderProbeEarly : ScriptBehaviour
{
    public override void Awake() => VerticalSliceBootstrap.Report("Order.Early(-100).Awake");
    public override void Start() => VerticalSliceBootstrap.Report("Order.Early(-100).Start");
}

// override 無し（default = 0）
public sealed class ExecutionOrderProbeDefault : ScriptBehaviour
{
    public override void Awake() => VerticalSliceBootstrap.Report("Order.Default(0).Awake");
    public override void Start() => VerticalSliceBootstrap.Report("Order.Default(0).Start");
}

// 遅い順（正値）
[DefaultExecutionOrder(100)]
public sealed class ExecutionOrderProbeLate : ScriptBehaviour
{
    public override void Awake() => VerticalSliceBootstrap.Report("Order.Late(100).Awake");
    public override void Start() => VerticalSliceBootstrap.Report("Order.Late(100).Start");
}
