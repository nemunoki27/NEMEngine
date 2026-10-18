using NEMEngine;

namespace SandboxScripts;

// 命中位置への生成、マウス描画、親への追従、フェードの確認
[ScriptTypeID("e23b6695-4e09-47db-a093-a72930c471ad")]
public sealed class DecalDemo : MonoBehaviour {

    private sealed class Stamp {

        public GameObject owner = null!;
        public DecalProjector projector = null!;
        public LinkedListNode<Stamp> node = null!;
        public float age;
        public float alpha = 1.0f;
        public bool temporary;
    }

    [SerializeField, Label("最大保持数・0で無制限"), Min(0)]
    private int maxDecals = 0;
    [SerializeField, Label("寿命・秒・0で無期限"), Min(0)]
    private float lifetime = 0.0f;
    [SerializeField, Label("フェード時間・秒"), Min(0)]
    private float fadeDuration = 2.0f;
    [SerializeField, Label("線の太さ"), Min(0.001f)]
    private float paintWidth = 0.1f;
    [SerializeField, Label("デカールの色")]
    private Color4 decalColor = new Color4(0.6f, 0.045f, 0.025f, 1.0f);

    private readonly LinkedList<Stamp> stamps = new();
    private readonly Stack<Stamp> pool = new();
    private Material? material;
    private GameObject? movingTarget;
    private GameObject? paintTarget;
    private Vector3 targetOrigin;
    private Vector3 lastPaintPoint;
    private Vector3 lastPaintNormal;
    private Vector2 lastCursor;
    private int sequence;
    private int wallSequence;
    private bool moveTarget;
    private bool paintEnabled;
    private bool hasPaintCursor;

    private void Start() {

        material = GameObject.Find("DecalTemplate")?.GetComponent<DecalProjector>()?.material;
        movingTarget = GameObject.Find("MovingReceiver");
        if (movingTarget) targetOrigin = movingTarget.transform.position;
        Debug.Log("デカール確認: 1=壁へ生成 2=フェード 3=移動物体へ生成 4=32個生成 5=消去 6=マウス描画切替 M=物体の移動");
    }

    private void Update() {

        TrimStamps();
        if (Input.GetKeyDown(KeyCode.Alpha1)) StampWall(false);
        if (Input.GetKeyDown(KeyCode.Alpha2)) StampWall(true);
        if (Input.GetKeyDown(KeyCode.Alpha3) && movingTarget) {
            CastStamp(movingTarget.transform.position - Vector3.forward * 4.0f, false);
        }
        if (Input.GetKeyDown(KeyCode.Alpha4)) {
            for (int i = 0; i < 32; ++i) StampWall(false);
        }
        if (Input.GetKeyDown(KeyCode.Alpha5)) {
            while (stamps.First != null) ReleaseStamp(stamps.First.Value);
            paintTarget = null;
            hasPaintCursor = false;
        }
        if (Input.GetKeyDown(KeyCode.Alpha6)) {
            paintEnabled = !paintEnabled;
            paintTarget = null;
            hasPaintCursor = false;
            Debug.Log(paintEnabled ? "マウス描画ON: 左クリックを押しながらなぞってください" : "マウス描画OFF");
        }
        if (Input.GetKeyDown(KeyCode.M)) moveTarget = !moveTarget;
        if (moveTarget && movingTarget) {
            movingTarget.transform.position = targetOrigin + new Vector3(Mathf.Sin(Time.time) * 1.5f, 0.0f, 0.0f);
            movingTarget.transform.rotation = Quaternion.Euler(0.0f, Mathf.Sin(Time.time * 0.7f) * 25.0f, 0.0f);
        }
        PaintCursor();
        UpdateStamps(Time.deltaTime);
    }

    // 現在の寿命設定で不透明度を更新する
    private void UpdateStamps(float deltaTime) {

        var node = stamps.First;
        while (node != null) {
            Stamp stamp = node.Value;
            node = node.Next;
            if (!stamp.owner || !stamp.owner.activeSelf) {
                ReleaseStamp(stamp);
                continue;
            }
            // 一時停止中はUpdateごと止まるので経過時間を維持できる
            stamp.age += Mathf.Max(0.0f, deltaTime);
            float duration = stamp.temporary && lifetime <= 0.0f ? 5.0f : Mathf.Max(0.0f, lifetime);
            if (duration > 0.0f && stamp.age >= duration) {
                ReleaseStamp(stamp);
                continue;
            }
            float fade = Mathf.Min(Mathf.Max(0.0f, fadeDuration), duration);
            float alpha = duration > 0.0f && fade > 0.0f ? Mathf.Clamp01((duration - stamp.age) / fade) : 1.0f;
            if (stamp.alpha == alpha) continue;
            stamp.alpha = alpha;
            stamp.projector.fadeFactor = alpha;
        }
    }

    // 保持数を減らした場合は古い投影から取り除く
    private void TrimStamps() {

        if (maxDecals <= 0) return;
        while (stamps.Count > maxDecals) {
            Stamp stamp = stamps.First!.Value;
            stamps.RemoveFirst();
            if (stamp.owner) Destroy(stamp.owner);
        }
        while (stamps.Count + pool.Count > maxDecals) {
            Stamp stamp = pool.Pop();
            if (stamp.owner) Destroy(stamp.owner);
        }
    }

    // 消えた投影を次の配置用に戻す
    private void ReleaseStamp(Stamp stamp) {

        stamps.Remove(stamp.node);
        if (!stamp.owner) return;
        stamp.owner.SetActive(false);
        pool.Push(stamp);
    }

    // カーソルの移動区間を分割して投影先を調べる
    private void PaintCursor() {

        if (!paintEnabled || !material || !Input.GetMouseButton(0) ||
            !Camera.TryGetMousePositionInView(out Vector2 cursor)) {
            paintTarget = null;
            hasPaintCursor = false;
            return;
        }
        // 高速に動かした場合も判定回数を制限する
        Vector2 start = hasPaintCursor ? lastCursor : cursor;
        int steps = System.Math.Clamp((int)(Vector2.Distance(start, cursor) / 8.0f) + 1, 1, 32);
        for (int i = 1; i <= steps; ++i) {
            PaintPoint(Vector2.Lerp(start, cursor, (float)i / steps));
        }
        lastCursor = cursor;
        hasPaintCursor = true;
    }

    // 同じ面の命中点を線でつなぐ
    private void PaintPoint(Vector2 cursor) {

        if (!Camera.ScreenPointToRay(cursor, out Ray ray) ||
            !Physics.Raycast(ray, out RaycastHit hit, 200.0f,
                queryTriggerInteraction: QueryTriggerInteraction.Ignore) || !hit.gameObject) {
            paintTarget = null;
            return;
        }
        Transform target = hit.gameObject.transform;
        Vector3 start = hit.point;
        // 別の面や空白をまたぐ区間はつながない
        if (paintTarget == hit.gameObject &&
            Vector3.Dot(hit.normal, target.TransformDirection(lastPaintNormal)) > 0.98f) {
            Vector3 previous = target.TransformPoint(lastPaintPoint);
            Vector3 delta = hit.point - previous;
            if (delta.sqrMagnitude < 0.000025f) return;
            if (Mathf.Abs(Vector3.Dot(delta, hit.normal)) < 0.025f) start = previous;
        }
        PlaceLine(hit, start);
        paintTarget = hit.gameObject;
        lastPaintPoint = target.InverseTransformPoint(hit.point);
        lastPaintNormal = target.InverseTransformDirection(hit.normal);
    }

    // 移動方向へ細い投影箱を伸ばす
    private void PlaceLine(RaycastHit hit, Vector3 start) {

        Vector3 delta = hit.point - start;
        float length = delta.magnitude;
        Quaternion rotation = length > 0.0001f
            ? Quaternion.LookRotation(-hit.normal, delta) : Quaternion.LookRotation(-hit.normal);
        // 両端を半幅ずつ重ねて曲がり角の隙間を埋める
        float width = Mathf.Max(0.001f, paintWidth);
        PlaceStamp(hit.gameObject!.transform, (start + hit.point) * 0.5f, rotation,
            new Vector3(width, length + width, 0.15f), false, true);
    }

    // 壁の確認位置へ順番にレイを飛ばす
    private void StampWall(bool fade) {

        int index = wallSequence++ % 32;
        CastStamp(new Vector3(-4.5f + index % 8 * 0.65f, 0.5f + index / 8 * 0.65f, 1.0f), fade);
    }

    // 前方の命中位置へ傷を配置する
    private void CastStamp(Vector3 origin, bool fade) {

        if (!material || !Physics.Raycast(origin, Vector3.forward, out RaycastHit hit, 10.0f) || !hit.gameObject) return;
        PlaceStamp(hit, fade);
    }

    // 確認用の傷を命中位置へ配置する
    private void PlaceStamp(RaycastHit hit, bool fade) {

        PlaceStamp(hit.gameObject!.transform, hit.point, Quaternion.LookRotation(-hit.normal),
            new Vector3(0.9f, 0.9f, 0.15f), fade, false);
    }

    // 再利用した投影の位置と見た目を更新する
    private void PlaceStamp(Transform target, Vector3 position, Quaternion rotation, Vector3 size, bool fade, bool line) {

        Stamp stamp = AcquireStamp();
        Transform transform = stamp.owner.transform;
        Transform? parent = transform.parent;
        // 親の変更が適用されるまでは現在の親を基準に設定する
        Vector3 scale = parent?.lossyScale ?? Vector3.one;
        transform.localScale = new Vector3(
            scale.x != 0.0f ? 1.0f / scale.x : 1.0f,
            scale.y != 0.0f ? 1.0f / scale.y : 1.0f,
            scale.z != 0.0f ? 1.0f / scale.z : 1.0f);
        transform.position = position;
        transform.rotation = rotation;
        if (parent != target) transform.SetParent(target, true);
        stamp.projector.material = material;
        stamp.projector.size = size;
        // 共有マテリアルを変えずに配置時の色を保存する
        stamp.projector.color = Color4.white;
        MaterialInstance parameters = stamp.projector.MaterialInstance;
        parameters.SetColor("color", decalColor);
        // 線には傷画像を使わず、通常の傷では元の画像へ戻す
        if (line) parameters.SetTexture("baseColorTexture", null);
        else parameters.Clear("baseColorTexture");
        stamp.projector.fadeFactor = 1.0f;
        stamp.projector.sortOrder = sequence++;
        stamp.age = 0.0f;
        stamp.alpha = 1.0f;
        stamp.temporary = fade;
        stamp.owner.SetActive(true);
        stamps.AddLast(stamp.node);
    }

    // 空きがなければ上限に応じて生成か再利用を選ぶ
    private Stamp AcquireStamp() {

        while (pool.TryPop(out Stamp? available)) {
            if (available.owner) return available;
        }
        if (maxDecals > 0 && stamps.Count >= maxDecals) {
            Stamp oldest = stamps.First!.Value;
            stamps.RemoveFirst();
            return oldest.owner ? oldest : CreateStamp();
        }
        return CreateStamp();
    }

    // 実行時用の投影Entityを作る
    private Stamp CreateStamp() {

        var owner = new GameObject("RuntimeDecal");
        var stamp = new Stamp { owner = owner, projector = owner.AddComponent<DecalProjector>() };
        stamp.node = new LinkedListNode<Stamp>(stamp);
        return stamp;
    }
}
