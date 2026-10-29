using NEMEngine;

public sealed class SplinePatrol : MonoBehaviour {

    [SerializeField] private GameObject? pathObject;
    [SerializeField, Min(0.0f)] private float speed = 2.0f;
    private Spline? path;
    private float distance;

    void Start() {
        // 経路を持つ別のGameObjectからSplineを取得
        if (pathObject != null) path = pathObject.GetComponent<Spline>();
    }

    void Update() {
        // 移動距離で曲線を評価し、World位置へ反映
        if (path == null || path.pointCount < 2) return;
        distance += speed * Time.deltaTime;
        transform.position = path.EvaluateDistance(distance, loop: true).position;
    }
}
