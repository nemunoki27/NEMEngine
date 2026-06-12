using NEMEngine;

namespace SandboxScripts;

[ScriptTypeId("c7e2046b-9d51-4a8c-bf03-7e6a1c92d4b5")]
public sealed class SampleMover : ScriptBehaviour
{

    [SerializedFieldId("1d6c4f80-2a11-4e63-9c07-5b2f8a0d1e44")]
    public float speed = 1.0f;

    [SerializedFieldId("2e7d5a91-3b22-4f74-ad18-6c30910e2f55")]
    [SerializeField]
    private Vector3 direction = new(1.0f, 0.0f, 0.0f);

    [SerializedFieldId("3f8e6ba2-4c33-4085-be29-7d41a21f3066")]
    [SerializeField]
    private Vector3 scaleUpValue = new(0.01f, 0.01f, 0.01f);

    [SerializedFieldId("40901cb3-5d44-4196-cf3a-8e52b3204177")]
    [SerializeField]
    private bool logDebugInput = true;

    [SerializedFieldId("51a12dc4-6e55-42a7-d04b-9f63c4315288")]
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

        // コメントを追加

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
