namespace NEMEngine;

public sealed class Transform {

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

    public void Translate(Vector3 translation) {
        position += translation;
    }

    public void TranslateLocal(Vector3 translation) {
        localPosition += translation;
    }
}
