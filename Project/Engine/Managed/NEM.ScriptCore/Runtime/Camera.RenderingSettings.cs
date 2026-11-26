using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	CameraRenderingSettings class
//	反射パラメータとCamera固有の追加Passを編集する
//============================================================================
public sealed class CameraRenderingSettings : SettingsObject {

	private readonly JsonObject originalReflection;

	internal CameraRenderingSettings(JsonObject values) : base(values) {
		originalReflection = GetReflectionParameters();
	}

	internal static CameraRenderingSettings Read(NativeEntity entity, int type) {
		var data = ComponentSettingsTransfer.Read(entity, type);
		data["reflectionParameters"] = GraphicsSettings.GetSettings().reflectionParameters;
		return new(data);
	}

	internal static void Apply(NativeEntity entity, int type, CameraRenderingSettings settings) {
		ArgumentNullException.ThrowIfNull(settings);
		ComponentSettingsTransfer.Apply(entity, type, settings.values);
		var reflection = settings.GetReflectionParameters();
		if (!JsonNode.DeepEquals(settings.originalReflection, reflection)) {
			GraphicsSettings.Change(0, new JsonObject { ["reflectionParameters"] = reflection });
		}
	}
	public RenderPassesSettings? renderPasses => values["renderPasses"] is JsonObject passes ? new(passes) : null;
	public JsonObject GetReflectionParameters() => (JsonObject)Child("reflectionParameters").DeepClone();
	public void SetReflectionParameters(JsonObject parameters) {
		ArgumentNullException.ThrowIfNull(parameters);
		values["reflectionParameters"] = parameters.DeepClone();
	}

	// 保存済みのPass名かIDとパラメータ名で反射値を更新する
	public bool SetReflectionParameter(string passName, string parameterName, float value) {
		if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
		foreach (var pass in (Child("reflectionParameters")["passes"] as JsonArray ?? new()).OfType<JsonObject>()) {
			if (pass["name"]?.GetValue<string>() != passName && pass["id"]?.GetValue<string>() != passName) continue;
			foreach (var parameter in (pass["parameters"] as JsonArray ?? new()).OfType<JsonObject>()) {
				if (parameter["name"]?.GetValue<string>() != parameterName) continue;
				parameter["value"] = value;
				return true;
			}
		}
		return false;
	}
}

public sealed partial class PerspectiveCamera {

	[Obsolete("GraphicsSettings.GetSettings().rayTracingReflections を使用してください")]
	public bool ReflectionEnabled {
		get => GraphicsSettings.GetSettings().rayTracingReflections;
		set => GraphicsSettings.Change(0, new JsonObject { ["allowDispatchRays"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().reflectionIntensity を使用してください")]
	public float ReflectionIntensity {
		get => GraphicsSettings.GetSettings().reflectionIntensity;
		set => GraphicsSettings.Change(0, new JsonObject { ["reflectionIntensity"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().reflectionMaxDistance を使用してください")]
	public float ReflectionMaxDistance {
		get => GraphicsSettings.GetSettings().reflectionMaxDistance;
		set => GraphicsSettings.Change(0, new JsonObject { ["reflectionMaxDistance"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().reflectionQuality を使用してください")]
	public int ReflectionQuality {
		get => GraphicsSettings.GetSettings().reflectionQuality;
		set => GraphicsSettings.Change(0, new JsonObject { ["reflectionQuality"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionEnabled を使用してください")]
	public bool AmbientOcclusionEnabled {
		get => GraphicsSettings.GetSettings().ambientOcclusionEnabled;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionEnabled"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionIntensity を使用してください")]
	public float AmbientOcclusionIntensity {
		get => GraphicsSettings.GetSettings().ambientOcclusionIntensity;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionIntensity"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionRadius を使用してください")]
	public float AmbientOcclusionRadius {
		get => GraphicsSettings.GetSettings().ambientOcclusionRadius;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionRadius"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionQuality を使用してください")]
	public int AmbientOcclusionQuality {
		get => GraphicsSettings.GetSettings().ambientOcclusionQuality;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionQuality"] = value });
	}

	public CameraRenderingSettings GetRenderingSettings() => CameraRenderingSettings.Read(native, componentTypeID);
	public void SetRenderingSettings(CameraRenderingSettings settings) => CameraRenderingSettings.Apply(native, componentTypeID, settings);
}

public sealed partial class OrthographicCamera {

	[Obsolete("GraphicsSettings.GetSettings().rayTracingReflections を使用してください")]
	public bool ReflectionEnabled {
		get => GraphicsSettings.GetSettings().rayTracingReflections;
		set => GraphicsSettings.Change(0, new JsonObject { ["allowDispatchRays"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().reflectionIntensity を使用してください")]
	public float ReflectionIntensity {
		get => GraphicsSettings.GetSettings().reflectionIntensity;
		set => GraphicsSettings.Change(0, new JsonObject { ["reflectionIntensity"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().reflectionMaxDistance を使用してください")]
	public float ReflectionMaxDistance {
		get => GraphicsSettings.GetSettings().reflectionMaxDistance;
		set => GraphicsSettings.Change(0, new JsonObject { ["reflectionMaxDistance"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().reflectionQuality を使用してください")]
	public int ReflectionQuality {
		get => GraphicsSettings.GetSettings().reflectionQuality;
		set => GraphicsSettings.Change(0, new JsonObject { ["reflectionQuality"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionEnabled を使用してください")]
	public bool AmbientOcclusionEnabled {
		get => GraphicsSettings.GetSettings().ambientOcclusionEnabled;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionEnabled"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionIntensity を使用してください")]
	public float AmbientOcclusionIntensity {
		get => GraphicsSettings.GetSettings().ambientOcclusionIntensity;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionIntensity"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionRadius を使用してください")]
	public float AmbientOcclusionRadius {
		get => GraphicsSettings.GetSettings().ambientOcclusionRadius;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionRadius"] = value });
	}
	[Obsolete("GraphicsSettings.GetSettings().ambientOcclusionQuality を使用してください")]
	public int AmbientOcclusionQuality {
		get => GraphicsSettings.GetSettings().ambientOcclusionQuality;
		set => GraphicsSettings.Change(0, new JsonObject { ["ambientOcclusionQuality"] = value });
	}

	public CameraRenderingSettings GetRenderingSettings() => CameraRenderingSettings.Read(native, componentTypeID);
	public void SetRenderingSettings(CameraRenderingSettings settings) => CameraRenderingSettings.Apply(native, componentTypeID, settings);
}
