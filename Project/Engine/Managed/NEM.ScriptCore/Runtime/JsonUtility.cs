using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization.Metadata;

namespace NEMEngine;

//============================================================================
//	JsonUtility class
//	ゲーム用データの保存FieldをJSONへ変換する
//============================================================================
public static class JsonUtility {

	// 保存対象のFieldだけを文字列へ変換する
	public static string ToJson(object value, bool prettyPrint = false) {
		ArgumentNullException.ThrowIfNull(value);
		ValidateType(value.GetType());
		return JsonSerializer.Serialize(value, value.GetType(), CreateOptions(prettyPrint));
	}

	// Componentを生成せずデータクラスを復元する
	public static T FromJson<T>(string json) {
		ValidateType(typeof(T));
		ArgumentNullException.ThrowIfNull(json);
		return JsonSerializer.Deserialize<T>(json, CreateOptions(false)) ?? throw new JsonException("JSONにデータがありません");
	}

	// 入力にないFieldを残し、変換成功後にまとめて反映する
	public static void FromJsonOverwrite(string json, object target) {
		ArgumentNullException.ThrowIfNull(target);
		ValidateType(target.GetType());
		using JsonDocument document = JsonDocument.Parse(json);
		if (document.RootElement.ValueKind != JsonValueKind.Object) throw new JsonException("JSONのルートはオブジェクトにしてください");
		var updates = new List<(FieldInfo field, object? value)>();
		var options = CreateOptions(false);
		foreach (FieldInfo field in ScriptFieldTypeUtility.EnumerateNestedSerializedFields(target.GetType())) {
			if (!document.RootElement.TryGetProperty(field.Name, out var value)) continue;
			// 入れ子の未指定Fieldも複製上で保持する
			object? current = field.GetValue(target);
			if (value.ValueKind == JsonValueKind.Object && current != null && !typeof(Asset).IsAssignableFrom(field.FieldType) &&
				ScriptFieldTypeUtility.HasSerializableFlag(field.FieldType)) {
				object clone = JsonSerializer.Deserialize(JsonSerializer.Serialize(current, field.FieldType, options), field.FieldType, options)!;
				FromJsonOverwrite(value.GetRawText(), clone);
				updates.Add((field, clone));
			} else updates.Add((field, value.Deserialize(field.FieldType, options)));
		}
		foreach (var update in updates) update.field.SetValue(target, update.value);
	}

	// Native参照を含むEngineオブジェクトの直接復元を防ぐ
	private static void ValidateType(Type type) {
		if (typeof(Object).IsAssignableFrom(type) || type.IsPrimitive || type.IsArray || type == typeof(string))
			throw new ArgumentException("JSONにはSerializableなデータクラスまたは構造体を指定してください");
		if (!ScriptFieldTypeUtility.HasSerializableFlag(type)) throw new ArgumentException($"Serializable属性が必要です: {type.FullName}");
	}

	// Scriptの保存と同じField選別を使用する
	private static JsonSerializerOptions CreateOptions(bool prettyPrint) {
		var resolver = new DefaultJsonTypeInfoResolver();
		resolver.Modifiers.Add(info => {
			if (typeof(Object).IsAssignableFrom(info.Type) && !typeof(Asset).IsAssignableFrom(info.Type))
				throw new ArgumentException("GameObjectやComponentはJSONデータへ直接保存できません");
			if (info.Kind != JsonTypeInfoKind.Object || !ScriptFieldTypeUtility.HasSerializableFlag(info.Type)) return;
			info.Properties.Clear();
			foreach (var field in ScriptFieldTypeUtility.EnumerateNestedSerializedFields(info.Type)) {
				var property = info.CreateJsonPropertyInfo(field.FieldType, field.Name);
				property.Get = field.GetValue;
				property.Set = field.SetValue;
				info.Properties.Add(property);
			}
		});
		var options = new JsonSerializerOptions { IncludeFields = true, IgnoreReadOnlyProperties = true,
			WriteIndented = prettyPrint, TypeInfoResolver = resolver };
		options.Converters.Add(new UUIDJsonConverter());
		options.Converters.Add(new AssetJsonConverterFactory());
		ScriptNumericConversion.Configure(options);
		return options;
	}
}
