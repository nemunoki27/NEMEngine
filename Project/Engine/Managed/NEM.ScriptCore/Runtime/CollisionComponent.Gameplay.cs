namespace NEMEngine;

// 自動生成されるCollisionComponent wrapperのgameplay拡張
public sealed partial class CollisionComponent {

    private CollisionShapeRef? shape_;
    public CollisionShapeRef Shape => shape_ ??= new CollisionShapeRef(this);

    internal NativeEntity NativeHandle => entity.native;

    // 現在フレームに接触しているか
    public bool IsColliding => NativeApi.ReadCollisionRuntimeState(entity.native);

    // propertyIdはnative側のCollisionGetShapePropertyと対応する
    public sealed class CollisionShapeRef {
        private readonly CollisionComponent r;
        internal CollisionShapeRef(CollisionComponent r) { this.r = r; }

        public ColliderShapeType Type {
            get => (ColliderShapeType)NativeApi.CollisionGetShapeInt(r.NativeHandle, 0);
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, 0, (int)value);
        }
        public bool Enabled {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, 1) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, 1, value ? 1 : 0);
        }
        public bool IsTrigger {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, 2) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, 2, value ? 1 : 0);
        }
        public bool UseTransformRotation {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, 3) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, 3, value ? 1 : 0);
        }
        public bool RotatedQuad {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, 4) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, 4, value ? 1 : 0);
        }
        public Vector3 Offset {
            get => NativeApi.CollisionGetShapeVector3(r.NativeHandle, 5);
            set => NativeApi.CollisionSetShapeVector3(r.NativeHandle, 5, value);
        }
        public Vector3 RotationDegrees {
            get => NativeApi.CollisionGetShapeVector3(r.NativeHandle, 6);
            set => NativeApi.CollisionSetShapeVector3(r.NativeHandle, 6, value);
        }
        public float Radius {
            get => NativeApi.CollisionGetShapeFloat(r.NativeHandle, 7);
            set => NativeApi.CollisionSetShapeFloat(r.NativeHandle, 7, value);
        }
        public Vector2 HalfSize2D {
            get => NativeApi.CollisionGetShapeVector2(r.NativeHandle, 8);
            set => NativeApi.CollisionSetShapeVector2(r.NativeHandle, 8, value);
        }
        public Vector3 HalfExtents3D {
            get => NativeApi.CollisionGetShapeVector3(r.NativeHandle, 9);
            set => NativeApi.CollisionSetShapeVector3(r.NativeHandle, 9, value);
        }
        public float CapsuleHeight {
            get => NativeApi.CollisionGetShapeFloat(r.NativeHandle, 10);
            set => NativeApi.CollisionSetShapeFloat(r.NativeHandle, 10, value);
        }
        public Vector2 CapsuleSize2D {
            get => NativeApi.CollisionGetShapeVector2(r.NativeHandle, 11);
            set => NativeApi.CollisionSetShapeVector2(r.NativeHandle, 11, value);
        }
        public CapsuleAxis CapsuleAxis {
            get => (CapsuleAxis)NativeApi.CollisionGetShapeInt(r.NativeHandle, 12);
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, 12, (int)value);
        }
    }
}
