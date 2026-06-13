using System.Text.Json;
using System.Text.Json.Serialization;

namespace NEMEngine;

// AssetRef<T> / EntityRef / ScriptRef<T> / UUID を authoring / runtime JSON へ相互変換する。
// readonly struct のため通常の property setter では復元できないので明示 converter を用意する。
// runtime pointer / index は一切保存せず、UUID と identity だけを round-trip する。

// UUID <-> 16桁hex 文字列（"" は None）
public sealed class UUIDJsonConverter : JsonConverter<UUID> {

    public override UUID Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {
        return reader.TokenType == JsonTokenType.String ? UUID.Parse(reader.GetString()) : UUID.None;
    }

    public override void Write(Utf8JsonWriter writer, UUID value, JsonSerializerOptions options) {
        writer.WriteStringValue(value.isValid ? value.ToString() : string.Empty);
    }
}

// EntityRef <-> { "kind":"Scene|Prefab|Null", "sourceAsset":"hex", "localFileId":"hex" }
public sealed class EntityRefJsonConverter : JsonConverter<EntityRef> {

    public override EntityRef Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return EntityRef.Null;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object) {
            return EntityRef.Null;
        }

        EntityRefKind kind = EntityRefKind.Null;
        if (root.TryGetProperty("kind", out JsonElement kindElement) && kindElement.ValueKind == JsonValueKind.String) {
            Enum.TryParse(kindElement.GetString(), out kind);
        }
        UUID source = root.TryGetProperty("sourceAsset", out JsonElement sa) ? UUID.Parse(sa.GetString()) : UUID.None;
        UUID local = root.TryGetProperty("localFileId", out JsonElement lf) ? UUID.Parse(lf.GetString()) : UUID.None;
        return new EntityRef(kind, source, local);
    }

    public override void Write(Utf8JsonWriter writer, EntityRef value, JsonSerializerOptions options) {

        writer.WriteStartObject();
        writer.WriteString("kind", value.kind.ToString());
        writer.WriteString("sourceAsset", value.sourceAsset.isValid ? value.sourceAsset.ToString() : string.Empty);
        writer.WriteString("localFileId", value.localFileId.isValid ? value.localFileId.ToString() : string.Empty);
        writer.WriteEndObject();
    }
}

// AssetRef<T> <-> { "assetId":"hex" }
public sealed class AssetRefJsonConverterFactory : JsonConverterFactory {

    public override bool CanConvert(Type typeToConvert) {
        return typeToConvert.IsGenericType && typeToConvert.GetGenericTypeDefinition() == typeof(AssetRef<>);
    }

    public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) {
        Type asset = typeToConvert.GetGenericArguments()[0];
        return (JsonConverter)Activator.CreateInstance(typeof(AssetRefJsonConverter<>).MakeGenericType(asset))!;
    }
}

public sealed class AssetRefJsonConverter<TAsset> : JsonConverter<AssetRef<TAsset>> where TAsset : class, IAssetType {

    public override AssetRef<TAsset> Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return AssetRef<TAsset>.None;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        JsonElement root = doc.RootElement;
        if (root.ValueKind == JsonValueKind.Object && root.TryGetProperty("assetId", out JsonElement id)) {
            return new AssetRef<TAsset>(UUID.Parse(id.GetString()));
        }
        return AssetRef<TAsset>.None;
    }

    public override void Write(Utf8JsonWriter writer, AssetRef<TAsset> value, JsonSerializerOptions options) {

        writer.WriteStartObject();
        writer.WriteString("assetId", value.id.isValid ? value.id.ToString() : string.Empty);
        writer.WriteEndObject();
    }
}

// ScriptRef<T> <-> { "entity":{...}, "scriptSlotId":"hex", "scriptTypeId":"guid" }
public sealed class ScriptRefJsonConverterFactory : JsonConverterFactory {

    public override bool CanConvert(Type typeToConvert) {
        return typeToConvert.IsGenericType && typeToConvert.GetGenericTypeDefinition() == typeof(ScriptRef<>);
    }

    public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) {
        Type script = typeToConvert.GetGenericArguments()[0];
        return (JsonConverter)Activator.CreateInstance(typeof(ScriptRefJsonConverter<>).MakeGenericType(script))!;
    }
}

public sealed class ScriptRefJsonConverter<T> : JsonConverter<ScriptRef<T>> where T : ScriptBehaviour {

    public override ScriptRef<T> Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return ScriptRef<T>.Null;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object) {
            return ScriptRef<T>.Null;
        }

        EntityRef entity = EntityRef.Null;
        if (root.TryGetProperty("entity", out JsonElement entityElement)) {
            entity = entityElement.Deserialize<EntityRef>(options);
        }
        UUID slot = root.TryGetProperty("scriptSlotId", out JsonElement s) ? UUID.Parse(s.GetString()) : UUID.None;
        string typeId = root.TryGetProperty("scriptTypeId", out JsonElement t) ? (t.GetString() ?? string.Empty) : string.Empty;
        return new ScriptRef<T>(entity, slot, typeId);
    }

    public override void Write(Utf8JsonWriter writer, ScriptRef<T> value, JsonSerializerOptions options) {

        writer.WriteStartObject();
        writer.WritePropertyName("entity");
        JsonSerializer.Serialize(writer, value.entity, options);
        writer.WriteString("scriptSlotId", value.scriptSlotId.isValid ? value.scriptSlotId.ToString() : string.Empty);
        writer.WriteString("scriptTypeId", value.scriptTypeId ?? string.Empty);
        writer.WriteEndObject();
    }
}
