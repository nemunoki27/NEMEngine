using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed unsafe partial class HexTiling {

	public float TileSize {
		get => TileSizeValue;
		set {
			if (!float.IsFinite(value) || value < 0.001f) throw new ArgumentOutOfRangeException(nameof(value));
			TileSizeValue = value;
		}
	}

	public float RandomRotation {
		get => RandomRotationValue;
		set {
			if (!float.IsFinite(value) || value < 0 || 180 < value) throw new ArgumentOutOfRangeException(nameof(value));
			RandomRotationValue = value;
		}
	}

	public bool IsSubMeshEnabled(int index) {
		var entries = ComponentSettingsTransfer.Read(native, componentTypeID)["subMeshEnabled"]!.AsArray();
		if (index < 0 || entries.Count <= index) throw new ArgumentOutOfRangeException(nameof(index));
		return entries[index]!.GetValue<bool>();
	}

	public void SetSubMeshEnabled(int index, bool enabled) {
		if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject { ["subMeshIndex"] = index, ["subMeshEnabled"] = enabled });
	}
}
