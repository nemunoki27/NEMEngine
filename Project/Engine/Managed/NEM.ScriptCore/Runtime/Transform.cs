namespace NEMEngine;

// Transform 操作の基準空間。Self は自身の回転(ローカル軸)、World はワールド軸。
public enum Space {

    Self,
    World
}

// Transform facade。owner GameObject の opaque handle だけを持ち、native Transform pointer は保持しない。
public sealed class Transform : Component, IComponentRef<Transform> {

    internal Transform(GameObject gameObject) {
        this.gameObject = gameObject;
    }

    public static int componentTypeID => 32;
    public static Transform FromEntity(GameObject gameObject) => new(gameObject);

    public Vector3 position {

        // world positionは現在のlocal値と親の継承設定から取得し、書き込み時は同じ親Transformでlocalへ変換される
        get => NativeTransformAPI.ReadPosition(native);
        set => NativeTransformAPI.WritePosition(native, value);
    }

    public Vector3 localPosition {

        // local SRTはTransformComponentの値を直接読み書きする
        get => NativeTransformAPI.ReadLocalPosition(native);
        set => NativeTransformAPI.WriteLocalPosition(native, value);
    }

    public Vector3 localScale {
        get => NativeTransformAPI.ReadLocalScale(native);
        set => NativeTransformAPI.WriteLocalScale(native, value);
    }

    public Quaternion localRotation {
        get => NativeTransformAPI.ReadLocalRotation(native);
        set => NativeTransformAPI.WriteLocalRotation(native, value);
    }

    // world回転。親階層の継承設定を反映し、setも同じ親回転でlocalへ変換される
    public Quaternion rotation {
        get => NativeTransformAPI.ReadRotation(native);
        set => NativeTransformAPI.WriteRotation(native, value);
    }

    // ワールド回転をZXYの角度で扱う
    public Vector3 eulerAngles {
        get => rotation.eulerAngles;
        set => rotation = Quaternion.Euler(value);
    }

    // ローカル回転をZXYの角度で扱う
    public Vector3 localEulerAngles {
        get => localRotation.eulerAngles;
        set => localRotation = Quaternion.Euler(value);
    }

    // world(lossy) scale。親階層の継承設定を反映した成分積の近似値（読み取り専用）
    public Vector3 lossyScale => NativeTransformAPI.ReadLossyScale(native);

    // 親(エンティティ階層 / スキンメッシュのジョイント)追従で回転を無視するか。座標は常に追従する
    public bool ignoreParentRotation {
        get => NativeTransformAPI.ReadIgnoreParentRotation(native);
        set => NativeTransformAPI.WriteIgnoreParentRotation(native, value);
    }

    // 親追従でスケールを無視するか
    public bool ignoreParentScale {
        get => NativeTransformAPI.ReadIgnoreParentScale(native);
        set => NativeTransformAPI.WriteIgnoreParentScale(native, value);
    }

    // world 回転を基準にした各方向ベクトル
    public Vector3 forward => rotation * Vector3.forward;
    public Vector3 right => rotation * Vector3.right;
    public Vector3 up => rotation * Vector3.up;

    public Transform? parent {
        get {
            GameObject? parentEntity = gameObject.parent;
            return parentEntity != null ? parentEntity.transform : null;
        }
        set => SetParent(value);
    }

    // 親の変更後もworld座標を保つか指定する
    public void SetParent(Transform? parent, bool worldPositionStays = true) {
        NativeEntityAPI.ReparentKeepWorld(native, parent is null ? NativeEntity.Null : parent.native, worldPositionStays);
    }

    // 子の数。firstChild / nextSibling を辿って数える（hierarchy は native 側が一貫管理）
    public int childCount {
        get {
            int count = 0;
            for (GameObject? child = gameObject.firstChild; child != null; child = child.nextSibling) {
                ++count;
            }
            return count;
        }
    }

    // 指定した子を取得し、範囲外は呼出し元へ返す
    public Transform GetChild(int index) {
        if (index < 0) {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        int current = 0;
        for (GameObject? child = gameObject.firstChild; child != null; child = child.nextSibling) {
            if (current == index) {
                return child.transform;
            }
            ++current;
        }
        throw new ArgumentOutOfRangeException(nameof(index));
    }

    // translation 分だけ移動する。Space.Self は自身の回転を掛けたローカル軸方向、Space.World はワールド軸方向。
    public void Translate(Vector3 translation, Space space = Space.Self) {
        if (space == Space.Self) {
            // 自身の world 回転でローカル軸へ写してから world position へ加算する
            position += rotation * translation;
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
        if (Vector3.Magnitude(direction) <= 1.0e-6f) {
            return;
        }
        rotation = Quaternion.LookRotation(Vector3.Normalize(direction), new Vector3(0.0f, 1.0f, 0.0f));
    }

    // position を world 空間の point として変換する（lossyScale → world 回転 → world 位置）。
    public Vector3 TransformPoint(Vector3 point) {
        // 親 scale の影響を含む近似 lossyScale を成分積で適用する
        Vector3 scaled = point * lossyScale;
        return position + rotation * scaled;
    }

    // world point をこの Transform のローカル空間へ戻す。
    public Vector3 InverseTransformPoint(Vector3 point) {
        Vector3 local = Quaternion.Inverse(rotation) * (point - position);
        Vector3 scale = lossyScale;
        // ゼロ scale 成分は 0 のまま返す（0 除算回避）
        return new Vector3(
            scale.x != 0.0f ? local.x / scale.x : 0.0f,
            scale.y != 0.0f ? local.y / scale.y : 0.0f,
            scale.z != 0.0f ? local.z / scale.z : 0.0f);
    }

    // direction を world 回転で写す（位置・scale の影響は受けない）。
    public Vector3 TransformDirection(Vector3 direction) {
        return rotation * direction;
    }

    // world direction をローカル空間へ戻す。
    public Vector3 InverseTransformDirection(Vector3 direction) {
        return Quaternion.Inverse(rotation) * direction;
    }

    // world position と world rotation を一括設定する（個別 setter と同じ内部実装へ委譲）。
    public void SetPositionAndRotation(Vector3 position, Quaternion rotation) {
        this.position = position;
        this.rotation = rotation;
    }

}
