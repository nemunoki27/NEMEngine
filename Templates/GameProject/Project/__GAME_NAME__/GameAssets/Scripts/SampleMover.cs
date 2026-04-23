using NEMEngine;

namespace __GAME_NAME__Scripts;

public sealed class SampleMover : ScriptBehaviour
{

    public float speed = 1.0f;

    [SerializeField]
    private Vector3 direction = new(1.0f, 0.0f, 0.0f);

    [SerializeField]
    private bool logDebugInput = true;

    [SerializeField]
    private Vector2 mouseLogOffset = Vector2.zero;

    private Vector3 initialLocalPosition;

    public override void Start()
    {

        initialLocalPosition = transform.localPosition;
        Debug.Log($"SampleMover Start entity={entity.name} localPosition={initialLocalPosition}");
    }

    public override void Update()
    {

        // ECS側のTransformをC#から操作して、フレーム時間に応じて移動する
        float moveSpeed = Input.GetKey(KeyCode.LeftShift) ? speed * 3.0f : speed;
        Vector3 position = transform.localPosition;
        position += direction * (moveSpeed * Time.deltaTime);
        transform.localPosition = position;

        if (!logDebugInput) {
            return;
        }

        if (Input.GetKeyDown(KeyCode.Space)) {
            Debug.Log($"Space pressed entity={entity.name} localPosition={transform.localPosition}");
        }
        if (Input.GetKeyDown(KeyCode.R)) {
            transform.localPosition = initialLocalPosition;
            Debug.Log($"Reset localPosition entity={entity.name} localPosition={transform.localPosition}");
        }
        if (Input.GetMouseButtonDown(MouseButton.Left)) {
            Debug.Log($"Left mouse position={Input.mousePosition + mouseLogOffset}");
        }
    }
}
