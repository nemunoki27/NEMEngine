using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	InputSettings class
//	実行中のPlayer割当とデッドゾーンを変更する
//============================================================================
public static class InputSettings {

	public static float deadZone {
		get => GraphicsSettings.Read(false)["inputDeadZone"]!.GetValue<float>();
		set {
			if (!float.IsFinite(value) || value < 0 || value >= 1) throw new ArgumentOutOfRangeException(nameof(value));
			GraphicsSettings.Change(0, new JsonObject { ["inputDeadZone"] = value });
		}
	}
	public static bool backgroundInputEnabled {
		get => Application.runInBackground;
		set => Application.runInBackground = value;
	}
	public static int GetGamepad(int player) { ValidatePlayer(player); return GraphicsSettings.Read(false)["playerGamepads"]![player]!.GetValue<int>(); }
	public static bool GetKeyboardMouse(int player) { ValidatePlayer(player); return GraphicsSettings.Read(false)["playerKeyboardMouse"]![player]!.GetValue<bool>(); }
	public static void AssignDevices(int player, int gamepad, bool keyboardMouse) {
		ValidatePlayer(player);
		if (gamepad < -1 || gamepad > 3) throw new ArgumentOutOfRangeException(nameof(gamepad));
		var current = GraphicsSettings.Read(false);
		var pads = (JsonArray)current["playerGamepads"]!.DeepClone();
		var keyboards = (JsonArray)current["playerKeyboardMouse"]!.DeepClone();
		pads[player] = gamepad; keyboards[player] = keyboardMouse;
		GraphicsSettings.Change(0, new JsonObject { ["playerGamepads"] = pads, ["playerKeyboardMouse"] = keyboards });
	}
	private static void ValidatePlayer(int player) {
		if (player < 0 || player >= 4) throw new ArgumentOutOfRangeException(nameof(player));
	}
}

public static class AudioSettings {

	public static bool playInBackground {
		get => GraphicsSettings.Read(false)["audioPlayInBackground"]!.GetValue<bool>();
		set => GraphicsSettings.Change(0, new JsonObject { ["audioPlayInBackground"] = value });
	}
}

public static partial class Physics {

	// 次の更新から衝突タイプの組み合わせを切り替える
	public static void IgnoreLayerCollision(int layer1, int layer2, bool ignore = true) {
		ValidateCollisionLayer(layer1); ValidateCollisionLayer(layer2);
		var rows = (JsonArray)GraphicsSettings.Read(false)["collisionPairs"]!.DeepClone();
		uint first = rows[layer1]!.GetValue<uint>();
		uint second = rows[layer2]!.GetValue<uint>();
		rows[layer1] = ignore ? first & ~(1u << layer2) : first | (1u << layer2);
		rows[layer2] = ignore ? second & ~(1u << layer1) : second | (1u << layer1);
		GraphicsSettings.Change(0, new JsonObject { ["collisionPairs"] = rows });
	}
	public static bool GetIgnoreLayerCollision(int layer1, int layer2) {
		ValidateCollisionLayer(layer1); ValidateCollisionLayer(layer2);
		return (GraphicsSettings.Read(false)["collisionPairs"]![layer1]!.GetValue<uint>() & (1u << layer2)) == 0;
	}
	private static void ValidateCollisionLayer(int layer) {
		if (layer < 0 || layer >= 32) throw new ArgumentOutOfRangeException(nameof(layer));
	}
}
