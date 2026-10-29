using NEMEngine;

public sealed class GameObjectBasics : MonoBehaviour {

    [SerializeField] private Vector3 childOffset = new Vector3(0.0f, 1.0f, 0.0f);
    private GameObject? child;

    void Start() {
        // 自分を親にして子のGameObjectを生成
        child = new GameObject("Marker");
        child.transform.SetParent(transform, worldPositionStays: false);
        child.transform.localPosition = childOffset;
    }

    void OnDestroy() {
        // Scriptだけを外した場合も生成した子を破棄
        if (child != null) NEMEngine.Object.Destroy(child);
    }
}
