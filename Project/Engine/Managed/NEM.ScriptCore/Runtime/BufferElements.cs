using System.Runtime.InteropServices;

namespace NEMEngine;

// CollisionComponentが保持する1形状
[StructLayout(LayoutKind.Sequential)]
public struct CollisionShapeData :
    IBufferElementData<CollisionShapeData> {

    public static int componentTypeID => 38;

    public ColliderShapeType type;
    private byte enabled_;
    private byte isTrigger_;
    private byte useTransformRotation_;
    private byte rotatedQuad_;
    public Vector3 offset;
    public Vector3 rotationDegrees;
    public float radius;
    public Vector2 halfSize2D;
    public Vector3 halfExtents3D;
    public float capsuleHeight;
    public Vector2 capsuleSize2D;
    public CapsuleAxis capsuleAxis;

    public bool enabled {
        readonly get => enabled_ != 0;
        set => enabled_ = value ? (byte)1 : (byte)0;
    }
    public bool isTrigger {
        readonly get => isTrigger_ != 0;
        set => isTrigger_ = value ? (byte)1 : (byte)0;
    }
    public bool useTransformRotation {
        readonly get => useTransformRotation_ != 0;
        set => useTransformRotation_ = value ? (byte)1 : (byte)0;
    }
    public bool rotatedQuad {
        readonly get => rotatedQuad_ != 0;
        set => rotatedQuad_ = value ? (byte)1 : (byte)0;
    }

    public static CollisionShapeData Default {
        get {
            CollisionShapeData shape = new() {
                type = ColliderShapeType.Sphere3D,
                offset = Vector3.zero,
                rotationDegrees = Vector3.zero,
                radius = 0.5f,
                halfSize2D = new Vector2(0.5f, 0.5f),
                halfExtents3D = new Vector3(0.5f, 0.5f, 0.5f),
                capsuleHeight = 2.0f,
                capsuleSize2D = new Vector2(1.0f, 2.0f),
                capsuleAxis = CapsuleAxis.Y
            };
            shape.enabled = true;
            shape.useTransformRotation = true;
            return shape;
        }
    }
}

// LineRendererの点列要素、即時描画用LinePointとはindexの有無が異なる
[StructLayout(LayoutKind.Sequential)]
public struct LineRendererPoint :
    IBufferElementData<LineRendererPoint> {

    public static int componentTypeID => 42;

    public Vector3 position;
    public Color4 color;
    public float thickness;

    public LineRendererPoint(
        Vector3 position, Color4 color, float thickness = 1.0f) {

        this.position = position;
        this.color = color;
        this.thickness = thickness;
    }
}

// TextRendererの文字単位SRT
[StructLayout(LayoutKind.Sequential)]
public struct TextCharacterTransform :
    IBufferElementData<TextCharacterTransform> {

    public static int componentTypeID => 43;

    public Vector2 translation;
    public float rotation;
    public Vector2 scale;

    public static TextCharacterTransform Identity => new() {
        translation = Vector2.zero,
        rotation = 0.0f,
        scale = Vector2.one
    };
}
