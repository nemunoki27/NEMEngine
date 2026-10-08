namespace NEMEngine;

// 自動生成される PrimitiveRenderer wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// 形状ごとのパラメータは prim.Ring.EndAngle のようにネストしたアクセサ経由で扱う。
// 低レベルの生成 property（PlaneSize 等）は internal なので、ここから委譲する。
public sealed partial class PrimitiveRenderer {

    public MaterialInstance MaterialInstance =>
        new(gameObject, RendererMaterialTarget.Primitive);

    // 形状ごとのアクセサ、生成 wrapper 1 つにつき遅延生成してキャッシュする
    private PlaneAccessor? plane_;
    private CrossPlaneAccessor? crossPlane_;
    private RingAccessor? ring_;
    private CylinderAccessor? cylinder_;
    private SphereAccessor? sphere_;
    private HemisphereAccessor? hemisphere_;
    private CubeAccessor? cube_;

    public PlaneAccessor Plane => plane_ ??= new PlaneAccessor(this);
    public CrossPlaneAccessor CrossPlane => crossPlane_ ??= new CrossPlaneAccessor(this);
    public RingAccessor Ring => ring_ ??= new RingAccessor(this);
    public CylinderAccessor Cylinder => cylinder_ ??= new CylinderAccessor(this);
    public SphereAccessor Sphere => sphere_ ??= new SphereAccessor(this);
    public HemisphereAccessor Hemisphere => hemisphere_ ??= new HemisphereAccessor(this);
    public CubeAccessor Cube => cube_ ??= new CubeAccessor(this);

    // マテリアル color を上書きする。
    public void SetColor(Color4 color) {
        MaterialInstance.SetColor(MaterialParameterIDs.BaseColor, MaterialParameterNames.BaseColor, color);
    }
    public void SetColor(Color3 color) {
        SetColor(new Color4(color.r, color.g, color.b, 1.0f));
    }

    // 現在のマテリアル color を取得する。
    public Color4 GetColor() {
        return MaterialInstance.TryGetColor(MaterialParameterNames.BaseColor, out Color4 color)
            ? color
            : new Color4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Plane 形状パラメータ
    public sealed class PlaneAccessor {
        private readonly PrimitiveRenderer r;
        internal PlaneAccessor(PrimitiveRenderer r) { this.r = r; }
        public Vector2 Size { get => r.PlaneSize; set => r.PlaneSize = value; }
        public Vector2 Pivot { get => r.PlanePivot; set => r.PlanePivot = value; }
        public PrimitivePlaneAxis Axis { get => r.PlaneAxis; set => r.PlaneAxis = value; }
        public int DivideX { get => r.PlaneDivideX; set => r.PlaneDivideX = value; }
        public int DivideY { get => r.PlaneDivideY; set => r.PlaneDivideY = value; }
    }

    // CrossPlane 形状パラメータ
    public sealed class CrossPlaneAccessor {
        private readonly PrimitiveRenderer r;
        internal CrossPlaneAccessor(PrimitiveRenderer r) { this.r = r; }
        public Vector2 Size { get => r.CrossPlaneSize; set => r.CrossPlaneSize = value; }
        public Vector2 Pivot { get => r.CrossPlanePivot; set => r.CrossPlanePivot = value; }
        public int Count { get => r.CrossPlaneCount; set => r.CrossPlaneCount = value; }
    }

    // Ring 形状パラメータ
    public sealed class RingAccessor {
        private readonly PrimitiveRenderer r;
        internal RingAccessor(PrimitiveRenderer r) { this.r = r; }
        public float OuterRadius { get => r.RingOuterRadius; set => r.RingOuterRadius = value; }
        public float InnerRadius { get => r.RingInnerRadius; set => r.RingInnerRadius = value; }
        public float StartAngle { get => r.RingStartAngle; set => r.RingStartAngle = value; }
        public float EndAngle { get => r.RingEndAngle; set => r.RingEndAngle = value; }
        public int Divide { get => r.RingDivide; set => r.RingDivide = value; }
    }

    // Cylinder 形状パラメータ
    public sealed class CylinderAccessor {
        private readonly PrimitiveRenderer r;
        internal CylinderAccessor(PrimitiveRenderer r) { this.r = r; }
        public float TopRadius { get => r.CylinderTopRadius; set => r.CylinderTopRadius = value; }
        public float CenterRadius { get => r.CylinderCenterRadius; set => r.CylinderCenterRadius = value; }
        public float BottomRadius { get => r.CylinderBottomRadius; set => r.CylinderBottomRadius = value; }
        public float TopRadiusWeight { get => r.CylinderTopRadiusWeight; set => r.CylinderTopRadiusWeight = value; }
        public float BottomRadiusWeight { get => r.CylinderBottomRadiusWeight; set => r.CylinderBottomRadiusWeight = value; }
        public Color4 TopColor { get => r.CylinderTopColor; set => r.CylinderTopColor = value; }
        public Color4 CenterColor { get => r.CylinderCenterColor; set => r.CylinderCenterColor = value; }
        public Color4 BottomColor { get => r.CylinderBottomColor; set => r.CylinderBottomColor = value; }
        public float Height { get => r.CylinderHeight; set => r.CylinderHeight = value; }
        public float MaxAngle { get => r.CylinderMaxAngle; set => r.CylinderMaxAngle = value; }
        public int RadialDivide { get => r.CylinderRadialDivide; set => r.CylinderRadialDivide = value; }
        public int HeightDivide { get => r.CylinderHeightDivide; set => r.CylinderHeightDivide = value; }
        public PrimitiveCylinderCap Cap { get => r.CylinderCap; set => r.CylinderCap = value; }
        public PrimitiveCylinderUVMode UVMode { get => r.CylinderUVMode; set => r.CylinderUVMode = value; }
    }

    // Sphere 形状パラメータ
    public sealed class SphereAccessor {
        private readonly PrimitiveRenderer r;
        internal SphereAccessor(PrimitiveRenderer r) { this.r = r; }
        public float Radius { get => r.SphereRadius; set => r.SphereRadius = value; }
        public int LongitudeDivide { get => r.SphereLongitudeDivide; set => r.SphereLongitudeDivide = value; }
        public int LatitudeDivide { get => r.SphereLatitudeDivide; set => r.SphereLatitudeDivide = value; }
    }

    // Hemisphere 形状パラメータ
    public sealed class HemisphereAccessor {
        private readonly PrimitiveRenderer r;
        internal HemisphereAccessor(PrimitiveRenderer r) { this.r = r; }
        public float Radius { get => r.HemisphereRadius; set => r.HemisphereRadius = value; }
        public int LongitudeDivide { get => r.HemisphereLongitudeDivide; set => r.HemisphereLongitudeDivide = value; }
        public int LatitudeDivide { get => r.HemisphereLatitudeDivide; set => r.HemisphereLatitudeDivide = value; }
        public bool BottomCap { get => r.HemisphereBottomCap; set => r.HemisphereBottomCap = value; }
    }

    // Cube 形状パラメータ
    public sealed class CubeAccessor {
        private readonly PrimitiveRenderer r;
        internal CubeAccessor(PrimitiveRenderer r) { this.r = r; }
        public Vector3 Size { get => r.CubeSize; set => r.CubeSize = value; }
        public Vector3 Pivot { get => r.CubePivot; set => r.CubePivot = value; }
    }
}
