using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static unsafe class LocalTimeTests {

	private static string snapshot = "";
	private static string applied = "";
	private static int appliedType;

	internal static void Run() {
		var copy = NativeAPI.CopyComponentSettings;
		var apply = NativeAPI.ApplyComponentSettings;
		var delta = NativeAPI.GetLocalDeltaTime;
		var scale = NativeAPI.GetLocalTimeScale;
		NativeAPI.CopyComponentSettings = &Copy;
		NativeAPI.ApplyComponentSettings = &Apply;
		NativeAPI.GetLocalDeltaTime = &Delta;
		NativeAPI.GetLocalTimeScale = &Scale;
		try {
			snapshot = """{"timeSource":{"mode":"Explicit","source":{}},"timeSourceNative":{"world":{"index":4,"generation":7},"index":42,"generation":3}}""";
			NativeEntity native = new() { world = new() { index = 4, generation = 7 }, index = 1, generation = 2 };
			foreach (int type in new[] { 0, 8, 9, 26, 70, 71, 72 }) {
				var settings = ComponentTimeSource.Read(native, type);
				Check(settings.Mode == TimeSourceMode.Explicit && GameObject.RawNative(settings.Source).index == 42);
				ComponentTimeSource.Apply(native, type, settings);
				var value = JsonNode.Parse(applied)!;
				Check(appliedType == type && value["timeSourcePatch"]!.GetValue<bool>());
				Check(value["timeSourceNative"]!["generation"]!.GetValue<uint>() == 3);
				Check(value["timeSource"]!["mode"]!.GetValue<string>() == "Explicit");
				settings.Mode = TimeSourceMode.Unscaled;
				settings.Source = null;
				ComponentTimeSource.Apply(native, type, settings);
				value = JsonNode.Parse(applied)!;
				Check(value["timeSourceNative"]!["index"]!.GetValue<uint>() == uint.MaxValue);
				Check(value["timeSource"]!["mode"]!.GetValue<string>() == "Unscaled");
				Check(ComponentTimeSource.Read(native, type).Mode == TimeSourceMode.Explicit);
			}
			var owner = GameObject.FromNative(native)!;
			Check(Time.GetLocalDeltaTime(owner, TimeChannel.Timer) == 0.25f);
			Check(Time.GetLocalScale(owner, TimeChannel.Particle) == 0.5f);
			bool rejected = false;
			try { ComponentTimeSource.Apply(native, 71, new() { Mode = (TimeSourceMode)99 }); }
			catch (ArgumentOutOfRangeException) { rejected = true; }
			Check(rejected);
		} finally {
			NativeAPI.CopyComponentSettings = copy;
			NativeAPI.ApplyComponentSettings = apply;
			NativeAPI.GetLocalDeltaTime = delta;
			NativeAPI.GetLocalTimeScale = scale;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Copy(NativeEntity entity, int type, byte* buffer, int capacity) {
		byte[] bytes = Encoding.UTF8.GetBytes(snapshot);
		if (buffer == null || capacity <= bytes.Length) return bytes.Length;
		bytes.CopyTo(new Span<byte>(buffer, capacity));
		return bytes.Length;
	}
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Apply(NativeEntity entity, int type, byte* buffer, int size) {
		appliedType = type;
		applied = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(buffer, size));
		return 1;
	}
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static float Delta(NativeEntity entity, uint channel) => entity.index == 1 && channel == (uint)TimeChannel.Timer ? 0.25f : 0;
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static float Scale(NativeEntity entity, uint channel) => entity.index == 1 && channel == (uint)TimeChannel.Particle ? 0.5f : 0;
	private static void Check(bool condition) {
		if (!condition) throw new InvalidOperationException("Local time settings bridge mismatch.");
	}
}
