using System.Text.Json.Nodes;

namespace NEMEngine;

// planeの形状設定を編集する
public sealed class PlaneGeometrySettings : SettingsObject {

	internal PlaneGeometrySettings(JsonObject values) : base(values) { }
	public Vector2 size { get => ReadVector2("size", Vector2.zero); set => WriteVector2("size", value); }
	public Vector2 pivot { get => ReadVector2("pivot", Vector2.zero); set => WriteVector2("pivot", value); }
	public PrimitivePlaneAxis axis { get => ReadEnum("axis", default(PrimitivePlaneAxis)); set => WriteEnum("axis", value); }
	public int divideX { get => Read("divideX", 0); set => Write("divideX", value); }
	public int divideY { get => Read("divideY", 0); set => Write("divideY", value); }
}

// crossPlaneの形状設定を編集する
public sealed class CrossPlaneGeometrySettings : SettingsObject {

	internal CrossPlaneGeometrySettings(JsonObject values) : base(values) { }
	public Vector2 size { get => ReadVector2("size", Vector2.zero); set => WriteVector2("size", value); }
	public Vector2 pivot { get => ReadVector2("pivot", Vector2.zero); set => WriteVector2("pivot", value); }
	public int planeCount { get => Read("planeCount", 0); set => Write("planeCount", value); }
}

// ringの形状設定を編集する
public sealed class RingGeometrySettings : SettingsObject {

	internal RingGeometrySettings(JsonObject values) : base(values) { }
	public float outerRadius { get => Read("outerRadius", 0f); set => Write("outerRadius", value); }
	public float innerRadius { get => Read("innerRadius", 0f); set => Write("innerRadius", value); }
	public float startAngle { get => Read("startAngle", 0f); set => Write("startAngle", value); }
	public float endAngle { get => Read("endAngle", 0f); set => Write("endAngle", value); }
	public int divide { get => Read("divide", 0); set => Write("divide", value); }
}

// cylinderの形状設定を編集する
public sealed class CylinderGeometrySettings : SettingsObject {

	internal CylinderGeometrySettings(JsonObject values) : base(values) { }
	public float topRadius { get => Read("topRadius", 0f); set => Write("topRadius", value); }
	public float centerRadius { get => Read("centerRadius", 0f); set => Write("centerRadius", value); }
	public float bottomRadius { get => Read("bottomRadius", 0f); set => Write("bottomRadius", value); }
	public float topRadiusWeight { get => Read("topRadiusWeight", 0f); set => Write("topRadiusWeight", value); }
	public float bottomRadiusWeight { get => Read("bottomRadiusWeight", 0f); set => Write("bottomRadiusWeight", value); }
	public Color4 topColor { get => ReadColor("topColor", Color4.white); set => WriteColor("topColor", value); }
	public Color4 centerColor { get => ReadColor("centerColor", Color4.white); set => WriteColor("centerColor", value); }
	public Color4 bottomColor { get => ReadColor("bottomColor", Color4.white); set => WriteColor("bottomColor", value); }
	public float height { get => Read("height", 0f); set => Write("height", value); }
	public float maxAngle { get => Read("maxAngle", 0f); set => Write("maxAngle", value); }
	public int radialDivide { get => Read("radialDivide", 0); set => Write("radialDivide", value); }
	public int heightDivide { get => Read("heightDivide", 0); set => Write("heightDivide", value); }
	public PrimitiveCylinderCap cap { get => ReadEnum("cap", default(PrimitiveCylinderCap)); set => WriteEnum("cap", value); }
	public PrimitiveCylinderUVMode uvMode { get => ReadEnum("uvMode", default(PrimitiveCylinderUVMode)); set => WriteEnum("uvMode", value); }
}

// sphereの形状設定を編集する
public sealed class SphereGeometrySettings : SettingsObject {

	internal SphereGeometrySettings(JsonObject values) : base(values) { }
	public float radius { get => Read("radius", 0f); set => Write("radius", value); }
	public int longitudeDivide { get => Read("longitudeDivide", 0); set => Write("longitudeDivide", value); }
	public int latitudeDivide { get => Read("latitudeDivide", 0); set => Write("latitudeDivide", value); }
}

// hemisphereの形状設定を編集する
public sealed class HemisphereGeometrySettings : SettingsObject {

	internal HemisphereGeometrySettings(JsonObject values) : base(values) { }
	public float radius { get => Read("radius", 0f); set => Write("radius", value); }
	public int longitudeDivide { get => Read("longitudeDivide", 0); set => Write("longitudeDivide", value); }
	public int latitudeDivide { get => Read("latitudeDivide", 0); set => Write("latitudeDivide", value); }
	public bool bottomCap { get => Read("bottomCap", false); set => Write("bottomCap", value); }
}

// cubeの形状設定を編集する
public sealed class CubeGeometrySettings : SettingsObject {

	internal CubeGeometrySettings(JsonObject values) : base(values) { }
	public Vector3 size { get => ReadVector3("size", Vector3.zero); set => WriteVector3("size", value); }
	public Vector3 pivot { get => ReadVector3("pivot", Vector3.zero); set => WriteVector3("pivot", value); }
}
