using System.Text;
using System.Text.Json.Nodes;

namespace NEMEngine;

public enum DisplayOutputMode { SDR, HDR10, ScRGB }
public enum AntialiasingMethod { None, FXAA }

//============================================================================
//	GraphicsSettingsData class
//	Projectの希望設定を変更用Snapshotとして保持する
//============================================================================
public sealed class GraphicsSettingsData {

	internal readonly JsonObject values;
	internal GraphicsSettingsData(JsonObject values) {
		this.values = values;
		// 描画設定の適用で入力や解像度を巻き戻さない
		foreach (string key in new[] { "targetFrameRate", "runInBackground", "width", "height", "inputDeadZone",
			"playerGamepads", "playerKeyboardMouse", "audioPlayInBackground", "queriesHitTriggers", "collisionPairs" }) values.Remove(key);
	}
	public bool useMeshShader { get => Read<bool>("allowMeshShader"); set => Write("allowMeshShader", value); }
	public AntialiasingMethod antialiasing {
		get => Enum.Parse<AntialiasingMethod>(Read<string>("antialiasing"));
		set => Write("antialiasing", value.ToString());
	}
	public float fxaaStrength { get => Read<float>("fxaaStrength"); set => Write("fxaaStrength", value); }
	public float fxaaEdgeThreshold { get => Read<float>("fxaaEdgeThreshold"); set => Write("fxaaEdgeThreshold", value); }
	public float fxaaMinimumThreshold { get => Read<float>("fxaaMinimumThreshold"); set => Write("fxaaMinimumThreshold", value); }
	public float fxaaSubpixelStrength { get => Read<float>("fxaaSubpixelStrength"); set => Write("fxaaSubpixelStrength", value); }
	public bool rayTracingShadows { get => Read<bool>("allowInlineRayTracing"); set => Write("allowInlineRayTracing", value); }
	public bool rayTracingReflections { get => Read<bool>("allowDispatchRays"); set => Write("allowDispatchRays", value); }
	public bool rayTracingDownsampling { get => Read<bool>("allowRaytracingDownsampling"); set => Write("allowRaytracingDownsampling", value); }
	public uint softShadowSampleCount { get => Read<uint>("softShadowSampleCount"); set => Write("softShadowSampleCount", value); }
	public bool frustumCulling { get => Read<bool>("allowFrustumCulling"); set => Write("allowFrustumCulling", value); }
	public bool occlusionCulling { get => Read<bool>("allowOcclusionCulling"); set => Write("allowOcclusionCulling", value); }
	public bool contributionCulling { get => Read<bool>("allowContributionCulling"); set => Write("allowContributionCulling", value); }
	public bool normalConeCulling { get => Read<bool>("allowNormalConeCulling"); set => Write("allowNormalConeCulling", value); }
	public bool doubleBuffering { get => Read<uint>("frameContextCount") == 2; set => Write("frameContextCount", value ? 2u : 1u); }
	public DisplayOutputMode displayOutput { get => Enum.Parse<DisplayOutputMode>(Read<string>("displayOutputMode")); set => Write("displayOutputMode", value.ToString()); }
	public float paperWhiteNits { get => Read<float>("paperWhiteNits"); set => Write("paperWhiteNits", value); }
	public Color4 clearColor {
		get {
			var color = values["sceneClearColor"]!.AsObject();
			return new(color["r"]!.GetValue<float>(), color["g"]!.GetValue<float>(), color["b"]!.GetValue<float>(), 1.0f);
		}
		set => values["sceneClearColor"] = new JsonObject { ["r"] = value.r, ["g"] = value.g, ["b"] = value.b, ["a"] = 1.0f };
	}
	public float maximumLuminanceNits { get => Read<float>("maxLuminanceNits"); set => Write("maxLuminanceNits", value); }
	internal T Read<T>(string key) => values[key]!.GetValue<T>();
	internal void Write<T>(string key, T value) => values[key] = JsonValue.Create(value);
}

//============================================================================
//	GraphicsRuntimeState class
//	GPUの能力と実際に使用中の描画経路を参照する
//============================================================================
public sealed class GraphicsRuntimeState {

	private readonly JsonObject values;
	internal GraphicsRuntimeState(JsonObject values) { this.values = values; }
	public bool meshShaderSupported => Read<bool>("meshShaderSupported");
	public AntialiasingMethod antialiasing => Enum.Parse<AntialiasingMethod>(Read<string>("antialiasing"));
	public uint renderWidth => Read<uint>("renderWidth");
	public uint renderHeight => Read<uint>("renderHeight");
	public uint outputWidth => Read<uint>("outputWidth");
	public uint outputHeight => Read<uint>("outputHeight");
	public bool rayTracingSupported => Read<bool>("rayTracingSupported");
	public bool inlineRayTracingSupported => Read<bool>("inlineRayTracingSupported");
	public bool meshShaderEnabled => Read<bool>("meshShaderEnabled");
	public bool rayTracingShadowsEnabled => Read<bool>("rayTracingShadowsEnabled");
	public bool rayTracingReflectionsEnabled => Read<bool>("rayTracingReflectionsEnabled");
	public bool doubleBufferingEnabled => Read<bool>("doubleBufferingEnabled");
	public DisplayOutputMode displayOutput => (DisplayOutputMode)Read<int>("displayOutputMode");
	public int presentTargetFrameRate => Read<int>("presentTargetFrameRate");
	private T Read<T>(string key) => values[key]!.GetValue<T>();
}

//============================================================================
//	GraphicsSettings class
//	実行中の描画設定と明示保存を操作する
//============================================================================
public static unsafe class GraphicsSettings {

	public static GraphicsSettingsData GetSettings() => new(Read(false));
	public static GraphicsRuntimeState GetEffectiveSettings() => new(Read(true));
	public static void ApplySettings(GraphicsSettingsData settings) {
		ArgumentNullException.ThrowIfNull(settings);
		Change(0, settings.values);
	}
	public static void SaveUserSettings() => Change(1);
	public static void LoadUserSettings() => Change(2);
	public static void ResetToProjectDefaults() => Change(3);

	// 希望値は適用待ちの変更も含めて取得する
	internal static JsonObject Read(bool effective) {
		if (NativeAPI.CopyRuntimeSettings == null) throw new NotSupportedException("描画設定APIが接続されていません");
		int size = NativeAPI.CopyRuntimeSettings(effective ? 1 : 0, null, 0);
		if (size < 0 || size > 1024 * 1024) throw new InvalidOperationException("描画設定を取得できません");
		byte[] bytes = new byte[size + 1];
		fixed (byte* data = bytes) {
			int written = NativeAPI.CopyRuntimeSettings(effective ? 1 : 0, data, bytes.Length);
			if (written < 0 || written > size) throw new InvalidOperationException("描画設定の転送に失敗しました");
			return JsonNode.Parse(Encoding.UTF8.GetString(bytes, 0, written))!.AsObject();
		}
	}

	// 検証に成功した要求だけをNativeへ渡す
	internal static void Change(int action, JsonObject? settings = null) {
		if (NativeAPI.ChangeRuntimeSettings == null) throw new NotSupportedException("描画設定APIが接続されていません");
		byte[] bytes = settings == null ? Array.Empty<byte>() : Encoding.UTF8.GetBytes(settings.ToJsonString());
		fixed (byte* data = bytes) {
			var status = (ManagedStatus)NativeAPI.ChangeRuntimeSettings(action, data, bytes.Length);
			if (status != ManagedStatus.Ok) throw new InvalidOperationException($"設定変更に失敗しました: {status}");
		}
	}
}
