namespace NEMEngine;

public sealed class Transform {

    private readonly Entity entity;

    internal Transform(Entity entity) {
        this.entity = entity;
    }

    public Vector3 position {

        // world positionはECSのworldMatrixから取得し、書き込み時は親Transformを考慮してlocalへ変換される
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
}
