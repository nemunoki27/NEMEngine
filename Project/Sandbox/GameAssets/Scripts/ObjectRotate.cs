using NEMEngine;

namespace SandboxScripts;

public sealed class ObjectRotate : ScriptBehaviour
{

    // 回転速度
    [SerializeField]
    private float rotateSpeed = 90.0f;

    // 振幅
    [SerializeField]
    private float amplitude = 0.5f;
    // 強さ
    [SerializeField]
    private float frequency = 2.0f;
    // 軸
    [SerializeField]
    private Vector3 rotateAxis = new Vector3(0.0f, 1.0f, 0.0f);

    // 経過時間
    private float elapsedTime = 0.0f;

    // 初期位置を保存
    private Vector3 initPos;

    public override void Awake()
    {
        initPos = transform.localPosition;
        elapsedTime = 0.0f;
    }

    public override void Update()
    {
        // Y軸を中心に回転させる
        float angle = Math.DegToRad(rotateSpeed * Time.deltaTime);
        Quaternion deltaRotation = Quaternion.MakeAxisAngle(rotateAxis.normalized, angle);
        transform.localRotation = Quaternion.Normalize(transform.localRotation * deltaRotation);

        // 経過時間
        elapsedTime += Time.deltaTime;

        // sin波で上下に移動させる
        float posY = MathF.Sin(elapsedTime * frequency) * amplitude;
        transform.localPosition = initPos + new Vector3(0.0f, posY, 0.0f);
    }
}