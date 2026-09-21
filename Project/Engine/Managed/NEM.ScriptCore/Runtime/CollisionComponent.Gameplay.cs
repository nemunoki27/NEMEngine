namespace NEMEngine;

// 自動生成されるCollisionComponent wrapperのgameplay拡張
public sealed partial class CollisionComponent {

    private CollisionShapeRef? shape_;
    public CollisionShapeRef Shape => shape_ ??= new CollisionShapeRef(this);

    internal NativeEntity NativeHandle => entity.native;

    // 現在フレームに接触しているか
    public bool IsColliding => NativeAPI.ReadCollisionRuntimeState(entity.native);

    // propertyIDはnative側のCollisionGetShapePropertyと対応する
    public sealed class CollisionShapeRef {
        private readonly CollisionComponent r;
        internal CollisionShapeRef(CollisionComponent r) { this.r = r; }

        public ColliderShapeType Type {
            get => (ColliderShapeType)NativeAPI.CollisionGetShapeInt(r.NativeHandle, 0);
            set => NativeAPI.CollisionSetShapeInt(r.NativeHandle, 0, (int)value);
        }
        public bool Enabled {
            get => NativeAPI.CollisionGetShapeInt(r.NativeHandle, 1) != 0;
            set => NativeAPI.CollisionSetShapeInt(r.NativeHandle, 1, value ? 1 : 0);
        }
        public bool IsTrigger {
            get => NativeAPI.CollisionGetShapeInt(r.NativeHandle, 2) != 0;
            set => NativeAPI.CollisionSetShapeInt(r.NativeHandle, 2, value ? 1 : 0);
        }
        public bool UseTransformRotation {
            get => NativeAPI.CollisionGetShapeInt(r.NativeHandle, 3) != 0;
            set => NativeAPI.CollisionSetShapeInt(r.NativeHandle, 3, value ? 1 : 0);
        }
        public bool RotatedQuad {
            get => NativeAPI.CollisionGetShapeInt(r.NativeHandle, 4) != 0;
            set => NativeAPI.CollisionSetShapeInt(r.NativeHandle, 4, value ? 1 : 0);
        }
        public Vector3 Offset {
            get => NativeAPI.CollisionGetShapeVector3(r.NativeHandle, 5);
            set => NativeAPI.CollisionSetShapeVector3(r.NativeHandle, 5, value);
        }
        public Vector3 RotationDegrees {
            get => NativeAPI.CollisionGetShapeVector3(r.NativeHandle, 6);
            set => NativeAPI.CollisionSetShapeVector3(r.NativeHandle, 6, value);
        }
        public float Radius {
            get => NativeAPI.CollisionGetShapeFloat(r.NativeHandle, 7);
            set => NativeAPI.CollisionSetShapeFloat(r.NativeHandle, 7, value);
        }
        public Vector2 HalfSize2D {
            get => NativeAPI.CollisionGetShapeVector2(r.NativeHandle, 8);
            set => NativeAPI.CollisionSetShapeVector2(r.NativeHandle, 8, value);
        }
        public Vector3 HalfExtents3D {
            get => NativeAPI.CollisionGetShapeVector3(r.NativeHandle, 9);
            set => NativeAPI.CollisionSetShapeVector3(r.NativeHandle, 9, value);
        }
        public float CapsuleHeight {
            get => NativeAPI.CollisionGetShapeFloat(r.NativeHandle, 10);
            set => NativeAPI.CollisionSetShapeFloat(r.NativeHandle, 10, value);
        }
        public Vector2 CapsuleSize2D {
            get => NativeAPI.CollisionGetShapeVector2(r.NativeHandle, 11);
            set => NativeAPI.CollisionSetShapeVector2(r.NativeHandle, 11, value);
        }
        public CapsuleAxis CapsuleAxis {
            get => (CapsuleAxis)NativeAPI.CollisionGetShapeInt(r.NativeHandle, 12);
            set => NativeAPI.CollisionSetShapeInt(r.NativeHandle, 12, (int)value);
        }
    }
}
