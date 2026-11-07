using System.Text.Json.Nodes;

namespace NEMEngine;

public enum RendererFillMode { WireFrame = 2, Solid = 3 }
public enum RendererCullMode { NONE = 1, FRONT = 2, BACK = 3 }
public enum RendererDepthComparison { NEVER = 1, LESS, EQUAL, LESS_EQUAL, GREATER, NOT_EQUAL, GREATER_EQUAL, ALWAYS }
public enum RendererStencilOperation { KEEP = 1, ZERO, REPLACE, INCR_SAT, DECR_SAT, INVERT, INCR, DECR }

public sealed class RendererStencilFace : SettingsObject {

	internal RendererStencilFace(JsonObject values) : base(values) { }
	public RendererDepthComparison comparison { get => ReadEnum("comparison", RendererDepthComparison.ALWAYS); set => WriteEnum("comparison", value); }
	public RendererStencilOperation fail { get => ReadEnum("fail", RendererStencilOperation.KEEP); set => WriteEnum("fail", value); }
	public RendererStencilOperation depthFail { get => ReadEnum("depthFail", RendererStencilOperation.KEEP); set => WriteEnum("depthFail", value); }
	public RendererStencilOperation pass { get => ReadEnum("pass", RendererStencilOperation.KEEP); set => WriteEnum("pass", value); }
}

// 変更は各RendererのSetRenderSettingsで一括適用する
public sealed class RendererRenderSettings : SettingsObject {

	public RendererRenderSettings() : base(new()) {

		inheritMaterial = true;
		fillMode = RendererFillMode.Solid;
		cullMode = RendererCullMode.BACK;
		depthTest = depthWrite = true;
		stencilEnabled = separateBackFace = false;
		depthComparison = RendererDepthComparison.LESS_EQUAL;
		stencilReference = 1;
		stencilReadMask = stencilWriteMask = 255;
		foreach (var face in new[] { frontFace, backFace }) {
			face.comparison = RendererDepthComparison.ALWAYS;
			face.fail = face.depthFail = face.pass = RendererStencilOperation.KEEP;
		}
	}
	internal RendererRenderSettings(JsonObject values) : base(values) { }
	public bool inheritMaterial { get => Read("inheritMaterial", true); set => Write("inheritMaterial", value); }
	public RendererFillMode fillMode { get => ReadEnum("fillMode", RendererFillMode.Solid); set => WriteEnum("fillMode", value); }
	public RendererCullMode cullMode { get => ReadEnum("cullMode", RendererCullMode.BACK); set => WriteEnum("cullMode", value); }
	public bool depthTest { get => Read("depthTest", true); set => Write("depthTest", value); }
	public bool depthWrite { get => Read("depthWrite", true); set => Write("depthWrite", value); }
	public bool stencilEnabled { get => Read("stencilEnabled", false); set => Write("stencilEnabled", value); }
	public bool separateBackFace { get => Read("separateBackFace", false); set => Write("separateBackFace", value); }
	public RendererDepthComparison depthComparison { get => ReadEnum("depthComparison", RendererDepthComparison.LESS_EQUAL); set => WriteEnum("depthComparison", value); }
	public byte stencilReference { get => Read<byte>("stencilReference", 1); set => Write("stencilReference", value); }
	public byte stencilReadMask { get => Read<byte>("stencilReadMask", 255); set => Write("stencilReadMask", value); }
	public byte stencilWriteMask { get => Read<byte>("stencilWriteMask", 255); set => Write("stencilWriteMask", value); }
	public RendererStencilFace frontFace => new(Child("frontFace"));
	public RendererStencilFace backFace => new(Child("backFace"));
}

internal static class ComponentRenderSettings {

	internal static RendererRenderSettings Read(NativeEntity entity, int type) =>
		new(ComponentSettingsTransfer.Read(entity, type)["renderSettings"]!.AsObject());

	internal static void Apply(NativeEntity entity, int type, RendererRenderSettings settings) {

		ArgumentNullException.ThrowIfNull(settings);
		ComponentSettingsTransfer.Apply(entity, type, new JsonObject {
			["renderSettingsPatch"] = true, ["renderSettings"] = settings.values.DeepClone()
		});
	}
}

public sealed partial class MeshRenderer {

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);
}

public sealed partial class PrimitiveRenderer {

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);
}

public sealed partial class SpriteRenderer {

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);
}

public sealed partial class TextRenderer {

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);
}

public sealed partial class LineRenderer {

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);
}

public sealed partial class ParticleSystem {

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);
}
