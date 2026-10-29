using NEMEngine;

public sealed class TriggerPickup : MonoBehaviour {

    [SerializeField, Min(0.01f)] private float radius = 0.75f;
    private bool collected;

    void Awake() {
        // 物体を押し戻さず、接触通知だけを受ける球を用意
        var collider = GetComponent<CollisionComponent>() ?? AddComponent<CollisionComponent>();
        collider.Enabled = true;
        collider.Shape.Type = ColliderShapeType.Sphere3D;
        collider.Shape.Enabled = true;
        collider.Shape.Radius = radius;
        collider.Shape.IsTrigger = true;
    }

    void OnCollisionEnter(Collision collision) {
        // Player以外の接触と、破棄適用前の重複取得を除外
        if (collected || collision.gameObject == null || !collision.gameObject.CompareTag("Player")) return;
        collected = true;

        // 所持数や得点を加える処理はこの位置へ接続
        Debug.Log("アイテムを取得しました");
        NEMEngine.Object.Destroy(gameObject);
    }
}
