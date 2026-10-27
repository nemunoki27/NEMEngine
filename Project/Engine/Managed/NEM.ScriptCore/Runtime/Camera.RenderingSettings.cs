using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	CameraRenderingSettings class
//	Camera固有の反射と追加Passを一括編集する
//============================================================================
public sealed class CameraRenderingSettings : SettingsObject {

	internal CameraRenderingSettings(JsonObject values) : base(values) { }
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

	public CameraRenderingSettings GetRenderingSettings() => new(ComponentSettingsTransfer.Read(native, componentTypeID));
	public void SetRenderingSettings(CameraRenderingSettings settings) {
		ArgumentNullException.ThrowIfNull(settings);
		ComponentSettingsTransfer.Apply(native, componentTypeID, settings.values);
	}
}

public sealed partial class OrthographicCamera {

	public CameraRenderingSettings GetRenderingSettings() => new(ComponentSettingsTransfer.Read(native, componentTypeID));
	public void SetRenderingSettings(CameraRenderingSettings settings) {
		ArgumentNullException.ThrowIfNull(settings);
		ComponentSettingsTransfer.Apply(native, componentTypeID, settings.values);
	}
}
