using NEMEngine;

public sealed class LaserLine : MonoBehaviour {

    [SerializeField] private GameObject? endPoint;
    [SerializeField, Min(0.001f)] private float width = 0.03f;
    private LineRenderer? line;
    private readonly LinePoint[] points = new LinePoint[2];

    void Start() {
        // 両端をWorld座標で渡す線として設定
        line = GetComponent<LineRenderer>();
        if (line == null) return;
        line.Is2D = false;
        line.UseWorldSpace = true;
        line.Loop = false;
    }

    void Update() {
        // 配列を作り直さず、移動した両端の位置だけを更新
        if (line == null || endPoint == null) return;
        var color = new Color4(1.0f, 0.2f, 0.1f, 1.0f);
        points[0] = new LinePoint(transform.position, color, width);
        points[1] = new LinePoint(endPoint.transform.position, color, width);
        line.SetPoints(points);
    }
}
