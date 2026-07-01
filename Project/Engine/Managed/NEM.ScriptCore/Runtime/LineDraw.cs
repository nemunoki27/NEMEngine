using System;
using System.Runtime.InteropServices;

namespace NEMEngine;

// ライン1点。座標と色と太さを持ち、ABI の ManagedLinePoint と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct LinePoint {

    public Vector3 position;
    public Color4 color;
    public float thickness;
    // LineRendererComponentの点列内での位置
    public int index;

    public LinePoint(Vector3 position, Color4 color, float thickness = 1.0f) {
        this.position = position;
        this.color = color;
        this.thickness = thickness;
        this.index = -1;
    }
}

// 即時ライン描画。呼んだフレームだけ GameView に描かれる。毎フレーム呼んで使う
public static class LineDraw {

    // 2点を結ぶ3Dライン
    public static void DrawLine(Vector3 start, Vector3 end, Color4 color, float thickness = 1.0f) {
        Span<LinePoint> points = stackalloc LinePoint[2];
        points[0] = new LinePoint(start, color, thickness);
        points[1] = new LinePoint(end, color, thickness);
        NativeApi.LineDrawImmediatePolyline(points, false, false, 0ul);
    }

    // 2点を結ぶ2Dライン。座標は2Dワールド基準
    public static void DrawLine2D(Vector2 start, Vector2 end, Color4 color, float thickness = 1.0f) {
        Span<LinePoint> points = stackalloc LinePoint[2];
        points[0] = new LinePoint(new Vector3(start.x, start.y, 0.0f), color, thickness);
        points[1] = new LinePoint(new Vector3(end.x, end.y, 0.0f), color, thickness);
        NativeApi.LineDrawImmediatePolyline(points, false, true, 0ul);
    }

    // 任意のポリラインを描く。loopで始点と終点をつなぐ
    public static void DrawPolyline(ReadOnlySpan<LinePoint> points, bool loop = false, bool is2D = false) {
        NativeApi.LineDrawImmediatePolyline(points, loop, is2D, 0ul);
    }

    // 組み込みのワイヤーフレーム球を描く
    public static void DrawSphere(Vector3 center, float radius, Color4 color, int division = 8, float thickness = 0.05f) {
        NativeApi.LineDrawImmediateSphere(center, radius, color, division, thickness, 0ul);
    }

    // 形状記述子の共通既定を作る
    private static NativeLineShape MakeShape(LineShapeType type, Color4 color, float thickness) {
        return new NativeLineShape {
            materialID = 0ul,
            shapeType = (int)type,
            division = 8,
            is2D = 0,
            radius = 1.0f,
            radius2 = 0.0f,
            height = 1.0f,
            thickness = thickness,
            rotation = new NativeQuaternion { x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f },
            color = NativeColor4.From(color)
        };
    }

    // 2D円、XY平面
    public static void DrawCircle(Vector2 center, float radius, Color4 color, int division = 16, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.Circle2D, color, thickness);
        shape.is2D = 1;
        shape.a = NativeVector3.From(new Vector3(center.x, center.y, 0.0f));
        shape.radius = radius;
        shape.division = division;
        NativeApi.LineDrawShapeImmediate(shape);
    }

    // 2D矩形、rotationDegreesはZ回転
    public static void DrawRect(Vector2 center, Vector2 size, Color4 color, float rotationDegrees = 0.0f, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.Rect2D, color, thickness);
        shape.is2D = 1;
        shape.a = NativeVector3.From(new Vector3(center.x, center.y, 0.0f));
        shape.b = NativeVector3.From(new Vector3(size.x, size.y, 0.0f));
        shape.rotation = NativeQuaternion.From(Quaternion.FromEulerDegrees(new Vector3(0.0f, 0.0f, rotationDegrees)));
        NativeApi.LineDrawShapeImmediate(shape);
    }

    // 半球
    public static void DrawHemisphere(Vector3 center, float radius, Quaternion rotation, Color4 color, int division = 8, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.Hemisphere, color, thickness);
        shape.a = NativeVector3.From(center);
        shape.radius = radius;
        shape.rotation = NativeQuaternion.From(rotation);
        shape.division = division;
        NativeApi.LineDrawShapeImmediate(shape);
    }

    // 軸平行ボックス
    public static void DrawAABB(Vector3 min, Vector3 max, Color4 color, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.AABB, color, thickness);
        shape.a = NativeVector3.From(min);
        shape.b = NativeVector3.From(max);
        NativeApi.LineDrawShapeImmediate(shape);
    }

    // 有向ボックス、sizeは各軸の半径
    public static void DrawOBB(Vector3 center, Vector3 size, Quaternion rotation, Color4 color, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.OBB, color, thickness);
        shape.a = NativeVector3.From(center);
        shape.b = NativeVector3.From(size);
        shape.rotation = NativeQuaternion.From(rotation);
        NativeApi.LineDrawShapeImmediate(shape);
    }

    // 円錐台
    public static void DrawCone(Vector3 center, float baseRadius, float topRadius, float height, Quaternion rotation, Color4 color, int division = 8, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.Cone, color, thickness);
        shape.a = NativeVector3.From(center);
        shape.radius = baseRadius;
        shape.radius2 = topRadius;
        shape.height = height;
        shape.rotation = NativeQuaternion.From(rotation);
        shape.division = division;
        NativeApi.LineDrawShapeImmediate(shape);
    }

    // 方向矢印
    public static void DrawArrow(Vector3 pos, float length, Quaternion rotation, Color4 color, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.Arrow, color, thickness);
        shape.a = NativeVector3.From(pos);
        shape.height = length;
        shape.rotation = NativeQuaternion.From(rotation);
        NativeApi.LineDrawShapeImmediate(shape);
    }

    // XYZ軸、色はX赤Y青Z緑で固定
    public static void DrawAxis(Vector3 pos, Quaternion rotation, float length = 1.0f, float thickness = 0.05f) {
        NativeLineShape shape = MakeShape(LineShapeType.Axis, Color4.white, thickness);
        shape.a = NativeVector3.From(pos);
        shape.height = length;
        shape.rotation = NativeQuaternion.From(rotation);
        NativeApi.LineDrawShapeImmediate(shape);
    }
}
