using System.Text.Json.Nodes;

namespace NEMEngine;

public enum ClothPinMode { Edge, Corners, Fasteners }
public enum ClothEdge { Left, Right, Top, Bottom }

public sealed partial class Cloth {

	public bool Enabled {
		get => Read<bool>("enabled");
		set => Write("enabled", value);
	}

	public bool PlayInEditMode {
		get => Read<bool>("playInEditMode");
		set => Write("playInEditMode", value);
	}

	public ClothPinMode PinMode {
		get => Enum.Parse<ClothPinMode>(Read<string>("pinMode"));
		set => Write("pinMode", value.ToString());
	}

	public ClothEdge Edge {
		get => Enum.Parse<ClothEdge>(Read<string>("edge"));
		set => Write("edge", value.ToString());
	}

	public int Fasteners {
		get => Read<int>("fasteners");
		set => Write("fasteners", value);
	}

	public float FoldDepth {
		get => Read<float>("foldDepth");
		set => Write("foldDepth", value);
	}

	public float StretchCompliance {
		get => Read<float>("stretchCompliance");
		set => Write("stretchCompliance", value);
	}

	public float ShearCompliance {
		get => Read<float>("shearCompliance");
		set => Write("shearCompliance", value);
	}

	public float BendCompliance {
		get => Read<float>("bendCompliance");
		set => Write("bendCompliance", value);
	}

	public float Damping {
		get => Read<float>("damping");
		set => Write("damping", value);
	}

	public float GravityScale {
		get => Read<float>("gravityScale");
		set => Write("gravityScale", value);
	}

	public float WindStrength {
		get => Read<float>("windStrength");
		set => Write("windStrength", value);
	}

	public float GustStrength {
		get => Read<float>("gustStrength");
		set => Write("gustStrength", value);
	}

	public float GustFrequency {
		get => Read<float>("gustFrequency");
		set => Write("gustFrequency", value);
	}

	public float Thickness {
		get => Read<float>("thickness");
		set => Write("thickness", value);
	}

	public Vector3 WindDirection {
		get {
			var value = ComponentSettingsTransfer.Read(native, componentTypeID)["windDirection"]!.AsObject();
			return new(value["x"]!.GetValue<float>(), value["y"]!.GetValue<float>(), value["z"]!.GetValue<float>());
		}
		set => ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject {
			["windDirection"] = new JsonObject { ["x"] = value.x, ["y"] = value.y, ["z"] = value.z }
		});
	}

	public uint CollisionMask {
		get => Read<uint>("collisionMask");
		set => Write("collisionMask", value);
	}

	public void Reset() => Write("reset", true);

	private T Read<T>(string key) => ComponentSettingsTransfer.Read(native, componentTypeID)[key]!.GetValue<T>();
	private void Write<T>(string key, T value) =>
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject { [key] = JsonValue.Create(value) });
}
