namespace NEMEngine;

// Transform 操作の基準空間。Self は自身の回転(ローカル軸)、World はワールド軸。
public enum Space {

    Self,
    World
}

// Transform facade。owner Entity の opaque handle だけを持ち、native Transform pointer は保持しない。
public sealed class Transform : Component, IComponentRef<Transform> {

    internal Transform(Entity entity) {
        this.entity = entity;
    }

    public static int componentTypeID => 32;
    public static Transform FromEntity(Entity entity) => new(entity);

    public Vector3 position {

        // world positionは現在のlocal値と親の継承設定から取得し、書き込み時は同じ親Transformでlocalへ変換される
        get => NativeApi.ReadPosition(entity.native);
        set => NativeApi.WritePosition(entity.native, value);
    }

    public Vector3 localPosition {

        // local SRTはTransformComponentの値を直接読み書きする
        get => NativeApi.ReadLocalPosition(entity.native);
        set => NativeApi.WriteLocalPosition(entity.native, value);
    }

    public Vector3 localScale {
        get => NativeApi.ReadLocalScale(entity.native);
        set => NativeApi.WriteLocalScale(entity.native, value);
    }

    public Quaternion localRotation {
        get => NativeApi.ReadLocalRotation(entity.native);
        set => NativeApi.WriteLocalRotation(entity.native, value);
    }

    // world回転。親階層の継承設定を反映し、setも同じ親回転でlocalへ変換される
    public Quaternion rotation {
        get => NativeApi.ReadRotation(entity.native);
        set => NativeApi.WriteRotation(entity.native, value);
    }

    // world(lossy) scale。親階層の継承設定を反映した成分積の近似値（読み取り専用）
    public Vector3 lossyScale => NativeApi.ReadLossyScale(entity.native);

    // 親(エンティティ階層 / スキンメッシュのジョイント)追従で回転を無視するか。座標は常に追従する
    public bool ignoreParentRotation {
        get => NativeApi.ReadIgnoreParentRotation(entity.native);
        set => NativeApi.WriteIgnoreParentRotation(entity.native, value);
    }

    // 親追従でスケールを無視するか
    public bool ignoreParentScale {
        get => NativeApi.ReadIgnoreParentScale(entity.native);
        set => NativeApi.WriteIgnoreParentScale(entity.native, value);
    }

    // world 回転を基準にした各方向ベクトル
    public Vector3 forward => RotateVector(rotation, new Vector3(0.0f, 0.0f, 1.0f));
    public Vector3 right => RotateVector(rotation, new Vector3(1.0f, 0.0f, 0.0f));
    public Vector3 up => RotateVector(rotation, new Vector3(0.0f, 1.0f, 0.0f));

    public Transform? parent {
        get {
            Entity parentEntity = entity.parent;
            return parentEntity.isAlive ? parentEntity.transform : null;
        }
        set => entity.SetParent(value?.entity ?? Entity.nullEntity);
    }

    public void SetParent(Transform? parent) {
        this.parent = parent;
    }

    // 親を付け替える。worldPositionStays=true なら付け替え前後で world transform を維持する（native の deferred 適用）。
    // default Entity を渡すと root 化する。循環/invalid parent は native 側で安全に拒否される。
    public void SetParent(Entity parent, bool worldPositionStays = true) {
        NativeApi.ReparentKeepWorld(entity.native, parent.native, worldPositionStays);
    }

    // 子の数。firstChild / nextSibling を辿って数える（hierarchy は native 側が一貫管理）
    public int childCount {
        get {
            int count = 0;
            for (Entity child = entity.firstChild; child.isAlive; child = child.nextSibling) {
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
        for (Entity child = entity.firstChild; child.isAlive; child = child.nextSibling) {
            if (current == index) {
                return child.transform;
            }
            ++current;
        }
        return null;
    }

    // translation 分だけ移動する。Space.Self は自身の回転を掛けたローカル軸方向、Space.World はワールド軸方向。
    public void Translate(Vector3 translation, Space space = Space.Self) {
        if (space == Space.Self) {
            // 自身の world 回転でローカル軸へ写してから world position へ加算する
            position += RotateVector(rotation, translation);
        } else {
            position += translation;
        }
    }

    // オイラー角(度)で回転を加える。Space.Self はローカル回転へ後乗算、Space.World はワールド回転へ前乗算。
    public void Rotate(Vector3 eulerAngles, Space space = Space.Self) {
        Rotate(Quaternion.FromEulerDegrees(eulerAngles), space);
    }

    // クォータニオンで回転を加える。Unity と同じく Self は localRotation*delta、World は delta*rotation。
    public void Rotate(Quaternion rotation, Space space = Space.Self) {
        if (space == Space.Self) {
            localRotation = Quaternion.Normalize(localRotation * rotation);
        } else {
            this.rotation = Quaternion.Normalize(rotation * this.rotation);
        }
    }

    // target を向く world 回転を設定する。target が現在位置とほぼ同じなら何もしない（ゼロ長 direction 対策）。
    public void LookAt(Vector3 target) {
        Vector3 direction = target - position;
        if (Vector3.Length(direction) <= 1.0e-6f) {
            return;
        }
        rotation = LookRotation(Vector3.Normalize(direction), new Vector3(0.0f, 1.0f, 0.0f));
    }

    // position を world 空間の point として変換する（lossyScale → world 回転 → world 位置）。
    public Vector3 TransformPoint(Vector3 point) {
        // 親 scale の影響を含む近似 lossyScale を成分積で適用する
        Vector3 scaled = point * lossyScale;
        return position + RotateVector(rotation, scaled);
    }

    // world point をこの Transform のローカル空間へ戻す。
    public Vector3 InverseTransformPoint(Vector3 point) {
        Vector3 local = RotateVector(Quaternion.Inverse(rotation), point - position);
        Vector3 scale = lossyScale;
        // ゼロ scale 成分は 0 のまま返す（0 除算回避）
        return new Vector3(
            scale.x != 0.0f ? local.x / scale.x : 0.0f,
            scale.y != 0.0f ? local.y / scale.y : 0.0f,
            scale.z != 0.0f ? local.z / scale.z : 0.0f);
    }

    // direction を world 回転で写す（位置・scale の影響は受けない）。
    public Vector3 TransformDirection(Vector3 direction) {
        return RotateVector(rotation, direction);
    }

    // world direction をローカル空間へ戻す。
    public Vector3 InverseTransformDirection(Vector3 direction) {
        return RotateVector(Quaternion.Inverse(rotation), direction);
    }

    // world position と world rotation を一括設定する（個別 setter と同じ内部実装へ委譲）。
    public void SetPositionAndRotation(Vector3 position, Quaternion rotation) {
        this.position = position;
        this.rotation = rotation;
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

    // forward / up から正規直交基底を作り、その回転行列を quaternion へ変換する（LookAt 用）。
    private static Quaternion LookRotation(Vector3 forward, Vector3 up) {
        Vector3 f = Vector3.Normalize(forward);
        Vector3 r = Cross(up, f);
        float rLen = Vector3.Length(r);
        // forward と up が平行なときは任意の右ベクトルへフォールバックする
        r = rLen <= 1.0e-6f ? Cross(new Vector3(0.0f, 0.0f, 1.0f), f) : r / rLen;
        if (Vector3.Length(r) <= 1.0e-6f) {
            r = Cross(new Vector3(1.0f, 0.0f, 0.0f), f);
        }
        r = Vector3.Normalize(r);
        Vector3 u = Cross(f, r);

        // 基底(r, u, f)を列に持つ回転行列から quaternion を復元する
        float m00 = r.x, m01 = u.x, m02 = f.x;
        float m10 = r.y, m11 = u.y, m12 = f.y;
        float m20 = r.z, m21 = u.z, m22 = f.z;
        float trace = m00 + m11 + m22;
        Quaternion q;
        if (trace > 0.0f) {
            float s = Math.Sqrt(trace + 1.0f) * 2.0f;
            q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
        } else if (m00 > m11 && m00 > m22) {
            float s = Math.Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
        } else if (m11 > m22) {
            float s = Math.Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
        } else {
            float s = Math.Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
        }
        return Quaternion.Normalize(q);
    }
}
