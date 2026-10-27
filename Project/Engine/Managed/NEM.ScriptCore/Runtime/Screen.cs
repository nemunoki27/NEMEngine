using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	Screen class
//	ゲーム画像の出力サイズを操作する
//============================================================================
public static class Screen {

	public static int width => GraphicsSettings.Read(false)["width"]!.GetValue<int>();
	public static int height => GraphicsSettings.Read(false)["height"]!.GetValue<int>();
	public static void SetResolution(int width, int height) {
		if (width < 1 || height < 1) throw new ArgumentOutOfRangeException(nameof(width), "画像サイズは1以上にしてください");
		GraphicsSettings.Change(0, new JsonObject { ["width"] = width, ["height"] = height });
	}
}
