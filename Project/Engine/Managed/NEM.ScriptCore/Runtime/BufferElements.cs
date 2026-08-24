using System.Runtime.InteropServices;

namespace NEMEngine;

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
