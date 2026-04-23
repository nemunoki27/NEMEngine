using NEMEngine;

namespace __GAME_NAME__Scripts;

public sealed class SampleMover : ScriptBehaviour
{

    public float speed = 1.0f;

    [SerializeField]
    private Vector3 direction = new(1.0f, 0.0f, 0.0f);

    public override void Update()
    {

        // ECS側のTransformをC#から操作して、フレーム時間に応じて移動する
        Vector3 position = transform.localPosition;
        position += direction * (speed * Time.deltaTime);
        transform.localPosition = position;
    }
}
