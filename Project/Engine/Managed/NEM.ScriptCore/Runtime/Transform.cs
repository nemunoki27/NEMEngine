namespace NEMEngine;

// Transform facade。owner Entity の opaque handle だけを持ち、native Transform pointer は保持しない。
// settable property(position/parent 等)を持つため struct ではなく class（一時 struct への代入を防ぐ）。
public sealed class Transform : IComponentRef {

    private readonly Entity owner;

    internal Transform(Entity entity) {
        owner = entity;
    }

    public Entity entity => owner;

    public Vector3 position {

        // world positionはECSのworldMatrixから取得し、書き込み時は親Transformを考慮してlocalへ変換される
        get => NativeApi.ReadPosition(owner.native);
        set => NativeApi.WritePosition(owner.native, value);
    }

    public Vector3 localPosition {

        // local SRTはTransformComponentの値を直接読み書きする
        get => NativeApi.ReadLocalPosition(owner.native);
        set => NativeApi.WriteLocalPosition(owner.native, value);
    }

    public Vector3 localScale {
        get => NativeApi.ReadLocalScale(owner.native);
        set => NativeApi.WriteLocalScale(owner.native, value);
    }

    public Quaternion localRotation {
        get => NativeApi.ReadLocalRotation(owner.native);
        set => NativeApi.WriteLocalRotation(owner.native, value);
    }

    // world(親階層を含めた) 回転。set は親の world 回転を考慮して local へ変換される
    public Quaternion rotation {
        get => NativeApi.ReadRotation(owner.native);
        set => NativeApi.WriteRotation(owner.native, value);
    }

    // world(lossy) scale。親階層の localScale を成分積で累積した近似値（読み取り専用）
    public Vector3 lossyScale => NativeApi.ReadLossyScale(owner.native);

    // world 回転を基準にした各方向ベクトル
    public Vector3 forward => RotateVector(rotation, new Vector3(0.0f, 0.0f, 1.0f));
    public Vector3 right => RotateVector(rotation, new Vector3(1.0f, 0.0f, 0.0f));
    public Vector3 up => RotateVector(rotation, new Vector3(0.0f, 1.0f, 0.0f));

    public Transform? parent {
        get {
            Entity parentEntity = owner.parent;
            return parentEntity.isAlive ? parentEntity.transform : null;
        }
        set => owner.SetParent(value?.entity ?? Entity.nullEntity);
    }

    public void SetParent(Transform? parent) {
        this.parent = parent;
    }

    // 子の数。firstChild / nextSibling を辿って数える（hierarchy は native 側が一貫管理）
    public int childCount {
        get {
            int count = 0;
            for (Entity child = owner.firstChild; child.isAlive; child = child.nextSibling) {
                ++count;
            }
            return count;
        }
    }

    // index 番目の子の Transform を返す。範囲外は null
    public Transform? GetChild(int index) {
        if (index < 0) {
            return null;
        }
        int current = 0;
        for (Entity child = owner.firstChild; child.isAlive; child = child.nextSibling) {
            if (current == index) {
                return child.transform;
            }
            ++current;
        }
        return null;
    }

    public void Translate(Vector3 translation) {
        position += translation;
    }

    public void TranslateLocal(Vector3 translation) {
        localPosition += translation;
    }

    // q による v の回転（v' = v + 2w(q.xyz×v) + 2 q.xyz×(q.xyz×v)）。MathTypes に無いためここで実装する
    private static Vector3 RotateVector(Quaternion q, Vector3 v) {
        Vector3 u = new(q.x, q.y, q.z);
        Vector3 t = Cross(u, v) * 2.0f;
        return v + t * q.w + Cross(u, t);
    }

    private static Vector3 Cross(Vector3 a, Vector3 b) {
        return new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }
}
