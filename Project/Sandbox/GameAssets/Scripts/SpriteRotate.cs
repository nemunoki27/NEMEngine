using NEMEngine;

namespace SandboxScripts;

public sealed class SpriteRotate : ScriptBehaviour
{

    // 回転速度
    [SerializeField]
    private float rotateSpeed = 90.0f;
    [SerializeField]
    private Vector3 rotateAxis = new Vector3(0.0f, 0.0f, 1.0f);

    public override void Start()
    {
    }

    public override void Update()
    {

        // Z軸を中心に回転させる
        float angle = Math.DegToRad(rotateSpeed * Time.deltaTime);
        Quaternion deltaRotation = Quaternion.MakeAxisAngle(new Vector3(0.0f, 0.0f, 1.0f), angle);
        transform.localRotation = Quaternion.Normalize(transform.localRotation * deltaRotation);
    }
}
