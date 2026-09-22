namespace NEMEngine;

// 自動生成されるCollisionComponent wrapperのgameplay拡張
public sealed partial class CollisionComponent {

    private CollisionShapeRef? shape_;
    public CollisionShapeRef Shape => shape_ ??= new CollisionShapeRef(this);

    internal NativeEntity NativeHandle => entity.native;

    // 現在フレームに接触しているか
    public bool IsColliding => NativePhysicsAPI.ReadCollisionRuntimeState(entity.native);

    // propertyIDはnative側のCollisionGetShapePropertyと対応する
    public sealed class CollisionShapeRef {
        private readonly CollisionComponent r;
        internal CollisionShapeRef(CollisionComponent r) { this.r = r; }

        public ColliderShapeType Type {
            get => (ColliderShapeType)NativePhysicsAPI.CollisionGetShapeInt(r.NativeHandle, 0);
            set => NativePhysicsAPI.CollisionSetShapeInt(r.NativeHandle, 0, (int)value);
        }
        public bool Enabled {
            get => NativePhysicsAPI.CollisionGetShapeInt(r.NativeHandle, 1) != 0;
            set => NativePhysicsAPI.CollisionSetShapeInt(r.NativeHandle, 1, value ? 1 : 0);
        }
        public bool IsTrigger {
            get => NativePhysicsAPI.CollisionGetShapeInt(r.NativeHandle, 2) != 0;
            set => NativePhysicsAPI.CollisionSetShapeInt(r.NativeHandle, 2, value ? 1 : 0);
        }
        public bool UseTransformRotation {
            get => NativePhysicsAPI.CollisionGetShapeInt(r.NativeHandle, 3) != 0;
            set => NativePhysicsAPI.CollisionSetShapeInt(r.NativeHandle, 3, value ? 1 : 0);
        }
        public bool RotatedQuad {
            get => NativePhysicsAPI.CollisionGetShapeInt(r.NativeHandle, 4) != 0;
            set => NativePhysicsAPI.CollisionSetShapeInt(r.NativeHandle, 4, value ? 1 : 0);
        }
        public Vector3 Offset {
            get => NativePhysicsAPI.CollisionGetShapeVector3(r.NativeHandle, 5);
            set => NativePhysicsAPI.CollisionSetShapeVector3(r.NativeHandle, 5, value);
        }
        public Vector3 RotationDegrees {
            get => NativePhysicsAPI.CollisionGetShapeVector3(r.NativeHandle, 6);
            set => NativePhysicsAPI.CollisionSetShapeVector3(r.NativeHandle, 6, value);
        }
        public float Radius {
            get => NativePhysicsAPI.CollisionGetShapeFloat(r.NativeHandle, 7);
            set => NativePhysicsAPI.CollisionSetShapeFloat(r.NativeHandle, 7, value);
        }
        public Vector2 HalfSize2D {
            get => NativePhysicsAPI.CollisionGetShapeVector2(r.NativeHandle, 8);
            set => NativePhysicsAPI.CollisionSetShapeVector2(r.NativeHandle, 8, value);
        }
        public Vector3 HalfExtents3D {
            get => NativePhysicsAPI.CollisionGetShapeVector3(r.NativeHandle, 9);
            set => NativePhysicsAPI.CollisionSetShapeVector3(r.NativeHandle, 9, value);
        }
        public float CapsuleHeight {
            get => NativePhysicsAPI.CollisionGetShapeFloat(r.NativeHandle, 10);
            set => NativePhysicsAPI.CollisionSetShapeFloat(r.NativeHandle, 10, value);
        }
        public Vector2 CapsuleSize2D {
            get => NativePhysicsAPI.CollisionGetShapeVector2(r.NativeHandle, 11);
            set => NativePhysicsAPI.CollisionSetShapeVector2(r.NativeHandle, 11, value);
        }
        public CapsuleAxis CapsuleAxis {
            get => (CapsuleAxis)NativePhysicsAPI.CollisionGetShapeInt(r.NativeHandle, 12);
            set => NativePhysicsAPI.CollisionSetShapeInt(r.NativeHandle, 12, (int)value);
        }
    }
}
