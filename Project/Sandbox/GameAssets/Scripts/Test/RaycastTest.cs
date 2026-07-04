namespace GameScripts;

// Physics.Raycastの動作確認用スクリプト
//
// 使い方:
//   1. 任意のEntityへこのスクリプトをattachしてPlayする
//   2. シーンにCollisionComponent付きEntity(Sphere/AABB/OBB)やFillMeshRendererComponentを置く
//   3. 動作:
//      - 毎フレーム、自Entityから真下へレイを飛ばして接地判定し、レイとヒット点を描画する
//      - マウスをGameViewに乗せると、マウスレイのヒット点へ球とレイを描画しConsoleへログを出す
//        (FillMeshにヒットした場合はtriangleIndexが出る)
//      - 左クリックでRaycastAllの全ヒットをConsoleへ一覧する
public class RaycastTest : ScriptBehaviour {

    [SeparatorText("設定")]
    // 接地判定レイの最大距離
    [SerializeField] private float groundCheckDistance = 10.0f;
    // 対象レイヤーのCollisionタイプ名、空なら全レイヤー
    [SerializeField] private List<string>? layerNames;
    // 判定対象、FillMeshだけ調べたい場合はFillMeshesにする
    [SerializeField] private RaycastTargets targets = RaycastTargets.All;

    // 描画色
    private static readonly Color4 yellow = new(1.0f, 1.0f, 0.0f, 1.0f);
    private static readonly Color4 cyan = new(0.0f, 1.0f, 1.0f, 1.0f);
    private static readonly Color4 gray = new(0.5f, 0.5f, 0.5f, 1.0f);

    // ログの毎フレームスパム防止
    private bool lastGrounded;
    private Entity lastMouseHitEntity;

    public override void Update() {

        uint layerMask = ResolveLayerMask();

        //--------------------------------------------------------------------
        // 1. 真下への接地判定レイ
        //--------------------------------------------------------------------
        Vector3 origin = transform.position;
        Vector3 down = new(0.0f, -1.0f, 0.0f);
        bool grounded = Physics.Raycast(origin, down, out RaycastHit groundHit, groundCheckDistance, layerMask, targets);

        Debug.DrawRay(origin, down * groundCheckDistance, grounded ? Color4.green : Color4.red);
        if (grounded) {
            LineDraw.DrawSphere(groundHit.point, 0.1f, Color4.green);
            Debug.DrawRay(groundHit.point, groundHit.normal, cyan);
        }
        if (grounded != lastGrounded) {
            lastGrounded = grounded;
            Debug.Log(grounded
                ? $"[RaycastTest] 接地: {groundHit.entity.name} distance={groundHit.distance:F3} normal=({groundHit.normal.x:F2},{groundHit.normal.y:F2},{groundHit.normal.z:F2})"
                : "[RaycastTest] 接地なし");
        }

        //--------------------------------------------------------------------
        // 2. マウスレイのピッキング
        //--------------------------------------------------------------------
        if (Camera.TryGetMouseRay(out Ray mouseRay)) {

            if (Physics.Raycast(mouseRay, out RaycastHit mouseHit, float.PositiveInfinity, layerMask, targets)) {

                Debug.DrawLine(mouseRay.origin, mouseHit.point, yellow);
                LineDraw.DrawSphere(mouseHit.point, 0.15f, yellow);
                Debug.DrawRay(mouseHit.point, mouseHit.normal, cyan);

                // ヒット対象が変わった時だけログを出す
                if (mouseHit.entity != lastMouseHitEntity) {
                    lastMouseHitEntity = mouseHit.entity;
                    string detail = mouseHit.triangleIndex >= 0
                        ? $"FillMesh triangleIndex={mouseHit.triangleIndex}"
                        : $"shapeIndex={mouseHit.shapeIndex} trigger={mouseHit.isTrigger}";
                    Debug.Log($"[RaycastTest] マウスヒット: {mouseHit.entity.name} distance={mouseHit.distance:F3} {detail}");
                }
            } else {
                lastMouseHitEntity = Entity.nullEntity;
                Debug.DrawRay(mouseRay.origin, mouseRay.direction * 100.0f, gray);
            }

            //----------------------------------------------------------------
            // 3. 左クリックでRaycastAllの一覧
            //----------------------------------------------------------------
            if (Input.GetMouseButtonDown(0)) {

                RaycastHit[] hits = Physics.RaycastAll(mouseRay, float.PositiveInfinity, layerMask, targets);
                Debug.Log($"[RaycastTest] RaycastAll: {hits.Length}件");
                foreach (RaycastHit hit in hits) {
                    string detail = hit.triangleIndex >= 0 ? $"triangle={hit.triangleIndex}" : $"shape={hit.shapeIndex}";
                    Debug.Log($"  {hit.entity.name} distance={hit.distance:F3} {detail}");
                }
            }
        }
    }

    // 設定されたCollisionタイプ名からマスクを作る、未設定は全レイヤー
    private uint ResolveLayerMask() {

        if (layerNames == null || layerNames.Count == 0) {
            return Physics.AllLayers;
        }
        uint mask = 0;
        foreach (string? name in layerNames) {
            if (!string.IsNullOrEmpty(name)) {
                mask |= Physics.GetLayerMask(name);
            }
        }
        return mask != 0 ? mask : Physics.AllLayers;
    }
}
