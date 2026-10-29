using NEMEngine;

public sealed class UVScroll : MonoBehaviour {

    [SerializeField] private Vector2 speed = new Vector2(0.2f, 0.0f);
    private UVTransform? uv;

    void Start() {
        // Textureの座標を変更するComponentを保持
        uv = GetComponent<UVTransform>();
    }

    void Update() {
        // 長時間動かしてもUV値が大きくなりすぎないよう周回
        if (uv == null) return;
        Vector2 position = uv.Position + speed * Time.deltaTime;
        position.x -= MathF.Floor(position.x);
        position.y -= MathF.Floor(position.y);
        uv.Position = position;
    }
}
