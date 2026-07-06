namespace NEMEngine;

// 自動生成される CollisionComponent wrapper の gameplay 拡張（生成ファイルは編集しない）。
// managedType が CollisionComponent なのは衝突イベント型 Collision と名前衝突を避けるため。
// shapes は vector なので generated property にできない。件数取得・追加・削除と、
// col.Shapes[i].Radius のようなインデックスアクセスを facade で提供する。
public sealed partial class CollisionComponent {

    // 衝突形状コレクション、遅延生成してキャッシュする
    private ShapeCollection? shapes_;
    public ShapeCollection Shapes => shapes_ ??= new ShapeCollection(this);

    // ネストアクセサへ渡す native handle
    internal NativeEntity NativeHandle => entity.native;

    // 形状数
    public int ShapeCount => NativeApi.GetCollisionShapeCount(entity.native);
    // 既定形状を末尾へ追加する
    public void AddShape() => NativeApi.AddCollisionShape(entity.native);
    // 指定indexの形状を削除する
    public void RemoveShapeAt(int index) => NativeApi.RemoveCollisionShapeAt(entity.native, index);
    // 全形状を削除する
    public void ClearShapes() => NativeApi.ClearCollisionShapes(entity.native);

    // Shapes[i] で個々の衝突形状へアクセスするコレクション
    public sealed class ShapeCollection {
        private readonly CollisionComponent r;
        internal ShapeCollection(CollisionComponent r) { this.r = r; }
        public int Count => r.ShapeCount;
        public CollisionShapeRef this[int index] => new CollisionShapeRef(r, index);
    }

    // 個々の衝突形状パラメータ、propertyId は native 側の CollisionGetShapeProperty と対応する
    public sealed class CollisionShapeRef {
        private readonly CollisionComponent r;
        private readonly int index;
        internal CollisionShapeRef(CollisionComponent r, int index) { this.r = r; this.index = index; }

        public ColliderShapeType Type {
            get => (ColliderShapeType)NativeApi.CollisionGetShapeInt(r.NativeHandle, index, 0);
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, index, 0, (int)value);
        }
        public bool Enabled {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, index, 1) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, index, 1, value ? 1 : 0);
        }
        public bool IsTrigger {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, index, 2) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, index, 2, value ? 1 : 0);
        }
        public bool UseTransformRotation {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, index, 3) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, index, 3, value ? 1 : 0);
        }
        public bool RotatedQuad {
            get => NativeApi.CollisionGetShapeInt(r.NativeHandle, index, 4) != 0;
            set => NativeApi.CollisionSetShapeInt(r.NativeHandle, index, 4, value ? 1 : 0);
        }
        public Vector3 Offset {
            get => NativeApi.CollisionGetShapeVector3(r.NativeHandle, index, 5);
            set => NativeApi.CollisionSetShapeVector3(r.NativeHandle, index, 5, value);
        }
        public Vector3 RotationDegrees {
            get => NativeApi.CollisionGetShapeVector3(r.NativeHandle, index, 6);
            set => NativeApi.CollisionSetShapeVector3(r.NativeHandle, index, 6, value);
        }
        public float Radius {
            get => NativeApi.CollisionGetShapeFloat(r.NativeHandle, index, 7);
            set => NativeApi.CollisionSetShapeFloat(r.NativeHandle, index, 7, value);
        }
        public Vector2 HalfSize2D {
            get => NativeApi.CollisionGetShapeVector2(r.NativeHandle, index, 8);
            set => NativeApi.CollisionSetShapeVector2(r.NativeHandle, index, 8, value);
        }
        public Vector3 HalfExtents3D {
            get => NativeApi.CollisionGetShapeVector3(r.NativeHandle, index, 9);
            set => NativeApi.CollisionSetShapeVector3(r.NativeHandle, index, 9, value);
        }
    }
}
