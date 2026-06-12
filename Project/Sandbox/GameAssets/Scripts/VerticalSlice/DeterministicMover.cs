using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	DeterministicMover
//	Transform facade と Time.deltaTime だけで動く deterministic な往復移動 probe
//============================================================================
public sealed class DeterministicMover : ScriptBehaviour
{
    [Tooltip("往復の振幅（ローカル空間）")]
    [SerializeField] private float amplitude = 1.0f;
    [Tooltip("往復の角速度（rad/s）")]
    [SerializeField] private float angularSpeed = 2.0f;
    [SerializeField] private Vector3 axis = new(1.0f, 0.0f, 0.0f);

    private Vector3 basePosition;
    private float phase;

    public override void Start()
    {
        basePosition = transform.localPosition;
        phase = 0.0f;
        VerticalSliceBootstrap.Report($"Mover.Start base={basePosition}");
    }

    public override void Update()
    {
        // 位相を deltaTime で進め、sin で往復させる（決定的）
        phase += angularSpeed * Time.deltaTime;
        float displacement = amplitude * MathF.Sin(phase);
        transform.localPosition = basePosition + axis * displacement;
    }
}
