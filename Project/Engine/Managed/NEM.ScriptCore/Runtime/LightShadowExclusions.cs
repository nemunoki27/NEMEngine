using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed class LightShadowExclusion {

	public GameObject? Target { get; }
	public bool IncludeChildren { get; }

	internal LightShadowExclusion(GameObject? target, bool includeChildren) {
		Target = target;
		IncludeChildren = includeChildren;
	}
}

internal static class LightShadowExclusionTransfer {

	private static readonly JsonSerializerOptions Options = new() { IncludeFields = true };

	internal static IReadOnlyList<LightShadowExclusion> Read(NativeEntity entity, int type) {
		var data = ComponentSettingsTransfer.Read(entity, type);
		var entries = data["shadowExclusions"]!.AsArray();
		var targets = data["shadowExclusionTargets"]!.AsArray();
		var result = new LightShadowExclusion[entries.Count];
		for (int index = 0; index < result.Length; ++index) {
			result[index] = new(GameObject.FromNative(targets[index]!.Deserialize<NativeEntity>(Options)),
				entries[index]!["includeChildren"]!.GetValue<bool>());
		}
		return result;
	}

	internal static void Apply(NativeEntity entity, int type, string operation, GameObject? target = null,
		bool includeChildren = false) {
		if (operation != "Clear") ArgumentNullException.ThrowIfNull(target);
		ComponentSettingsTransfer.Apply(entity, type, new JsonObject {
			["lightShadowExclusionPatch"] = operation,
			["targetNative"] = JsonSerializer.SerializeToNode(GameObject.RawNative(target), Options),
			["includeChildren"] = includeChildren
		});
	}
}

public sealed partial class DirectionalLight {
	public IReadOnlyList<LightShadowExclusion> GetShadowExclusions() => LightShadowExclusionTransfer.Read(native, 7);
	public void SetShadowExclusion(GameObject target, bool includeChildren = false) => LightShadowExclusionTransfer.Apply(native, 7, "Set", target, includeChildren);
	public void RemoveShadowExclusion(GameObject target) => LightShadowExclusionTransfer.Apply(native, 7, "Remove", target);
	public void ClearShadowExclusions() => LightShadowExclusionTransfer.Apply(native, 7, "Clear");
}

public sealed partial class PointLight {
	public IReadOnlyList<LightShadowExclusion> GetShadowExclusions() => LightShadowExclusionTransfer.Read(native, 18);
	public void SetShadowExclusion(GameObject target, bool includeChildren = false) => LightShadowExclusionTransfer.Apply(native, 18, "Set", target, includeChildren);
	public void RemoveShadowExclusion(GameObject target) => LightShadowExclusionTransfer.Apply(native, 18, "Remove", target);
	public void ClearShadowExclusions() => LightShadowExclusionTransfer.Apply(native, 18, "Clear");
}

public sealed partial class SpotLight {
	public IReadOnlyList<LightShadowExclusion> GetShadowExclusions() => LightShadowExclusionTransfer.Read(native, 28);
	public void SetShadowExclusion(GameObject target, bool includeChildren = false) => LightShadowExclusionTransfer.Apply(native, 28, "Set", target, includeChildren);
	public void RemoveShadowExclusion(GameObject target) => LightShadowExclusionTransfer.Apply(native, 28, "Remove", target);
	public void ClearShadowExclusions() => LightShadowExclusionTransfer.Apply(native, 28, "Clear");
}

public sealed partial class RectLight {
	public IReadOnlyList<LightShadowExclusion> GetShadowExclusions() => LightShadowExclusionTransfer.Read(native, 59);
	public void SetShadowExclusion(GameObject target, bool includeChildren = false) => LightShadowExclusionTransfer.Apply(native, 59, "Set", target, includeChildren);
	public void RemoveShadowExclusion(GameObject target) => LightShadowExclusionTransfer.Apply(native, 59, "Remove", target);
	public void ClearShadowExclusions() => LightShadowExclusionTransfer.Apply(native, 59, "Clear");
}
