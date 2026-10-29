using NEMEngine;

public sealed class MouseRaycast : MonoBehaviour {

    void Update() {
        // クリック位置からCameraのRayを取得
        if (!Input.GetMouseButtonDown(0)) return;
        if (!Camera.TryGetMouseRay(out var ray)) return;
        // 100単位以内の当たり位置を調べる
        if (Physics.Raycast(ray, out var hit, 100.0f)) {
            Debug.Log($"命中位置: {hit.point}");
        }
    }
}
