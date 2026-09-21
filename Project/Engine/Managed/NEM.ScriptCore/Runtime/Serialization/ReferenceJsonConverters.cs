using System.Text.Json;
using System.Text.Json.Serialization;

namespace NEMEngine;

// Asset / Entity / Component / ScriptBehaviour 参照フィールドを authoring / runtime JSON へ相互変換する。
// runtime pointer / index は一切保存せず、UUID と identity だけを round-trip する。
// AssetRef={"assetId"} / EntityRef={"kind","sourceAsset","localFileId"}形式で保存する。
// C++側のInspector / PrefabReferenceRemapperも同じidentity形式を扱う。
// 読み込みはidentityを現在のworldの生きた参照へ解決する（未解決はnull / null Entity）。

// UUID <-> 16桁hex 文字列（"" は None）
public sealed class UUIDJsonConverter : JsonConverter<UUID> {

    public override UUID Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {
        return reader.TokenType == JsonTokenType.String ? UUID.Parse(reader.GetString()) : UUID.None;
    }

    public override void Write(Utf8JsonWriter writer, UUID value, JsonSerializerOptions options) {
        writer.WriteStringValue(value.isValid ? value.ToString() : string.Empty);
    }
}

// EntityRef(内部identity) <-> { "kind":"Scene|Prefab|Null", "sourceAsset":"hex", "localFileId":"hex" }
internal sealed class EntityRefJsonConverter : JsonConverter<EntityRef> {

    public override EntityRef Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return EntityRef.Null;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        return ReadIdentity(doc.RootElement);
    }

    public override void Write(Utf8JsonWriter writer, EntityRef value, JsonSerializerOptions options) {
        WriteIdentity(writer, value);
    }

    // JsonElementからidentityを読む（Entity/Component/Scriptコンバータと共用）
    internal static EntityRef ReadIdentity(JsonElement root) {

        if (root.ValueKind != JsonValueKind.Object) {
            return EntityRef.Null;
        }
        EntityRefKind kind = EntityRefKind.Null;
        if (root.TryGetProperty("kind", out JsonElement kindElement) && kindElement.ValueKind == JsonValueKind.String) {
            Enum.TryParse(kindElement.GetString(), out kind);
        }
        AssetGUID source = root.TryGetProperty("sourceAsset", out JsonElement sa) ?
            AssetGUID.Parse(sa.GetString()) : AssetGUID.None;
        UUID local = root.TryGetProperty("localFileId", out JsonElement lf) ? UUID.Parse(lf.GetString()) : UUID.None;
        return new EntityRef(kind, source, local);
    }

    internal static void WriteIdentity(Utf8JsonWriter writer, EntityRef value) {

        writer.WriteStartObject();
        writer.WriteString("kind", value.kind.ToString());
        writer.WriteString("sourceAsset", value.sourceAsset.isValid ? value.sourceAsset.ToString() : string.Empty);
        writer.WriteString("localFileId", value.localFileID.isValid ? value.localFileID.ToString() : string.Empty);
        writer.WriteEndObject();
    }
}

// Entity <-> identity JSON。読み込みは現在のworldの生きたEntityへ解決し、書き込みはSceneObjectのidentityへ逆引きする
internal sealed class EntityJsonConverter : JsonConverter<Entity> {

    public override Entity Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return Entity.nullEntity;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        return EntityRefJsonConverter.ReadIdentity(doc.RootElement).Resolve();
    }

    public override void Write(Utf8JsonWriter writer, Entity value, JsonSerializerOptions options) {

        EntityRef identity = value.isAlive
            ? NativeAPI.ReadEntityReferenceIdentity(value.native)
            : EntityRef.Null;
        EntityRefJsonConverter.WriteIdentity(writer, identity);
    }
}

// Asset派生クラス <-> { "assetId":"hex" }。nullも空identityのobjectとして書く（C++側の既定値形状と揃える）
internal sealed class AssetJsonConverterFactory : JsonConverterFactory {

    public override bool CanConvert(Type typeToConvert) {
        return typeof(Asset).IsAssignableFrom(typeToConvert) && !typeToConvert.IsAbstract;
    }

    public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) {
        return (JsonConverter)Activator.CreateInstance(typeof(AssetJsonConverter<>).MakeGenericType(typeToConvert))!;
    }
}

internal sealed class AssetJsonConverter<TAsset> : JsonConverter<TAsset> where TAsset : Asset {

    // ctorはinternalのためreflectionで一度だけ引く
    private static readonly System.Reflection.ConstructorInfo? ctor = typeof(TAsset).GetConstructor(
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic,
        new[] { typeof(AssetGUID) });

    public override bool HandleNull => true;

    public override TAsset? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return null;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("assetId", out JsonElement id)) {
            return null;
        }
        AssetGUID assetID = AssetGUID.Parse(id.GetString());
        return assetID.isValid && ctor != null ? (TAsset)ctor.Invoke(new object[] { assetID }) : null;
    }

    public override void Write(Utf8JsonWriter writer, TAsset? value, JsonSerializerOptions options) {

        writer.WriteStartObject();
        writer.WriteString("assetId", value != null && value.id.isValid ? value.id.ToString() : string.Empty);
        writer.WriteEndObject();
    }
}

// 組込みcomponentクラス <-> { "entity":{...} }（型はフィールド宣言で決まるため値には保存しない）
internal sealed class ComponentJsonConverterFactory : JsonConverterFactory {

    public override bool CanConvert(Type typeToConvert) {
        return typeof(Component).IsAssignableFrom(typeToConvert)
            && !typeof(ScriptBehaviour).IsAssignableFrom(typeToConvert)
            && !typeToConvert.IsAbstract
            && typeof(IComponentRef<>).MakeGenericType(typeToConvert).IsAssignableFrom(typeToConvert);
    }

    public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) {
        return (JsonConverter)Activator.CreateInstance(typeof(ComponentJsonConverter<>).MakeGenericType(typeToConvert))!;
    }
}

internal sealed class ComponentJsonConverter<T> : JsonConverter<T> where T : Component, IComponentRef<T> {

    public override bool HandleNull => true;

    public override T? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return null;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("entity", out JsonElement entityElement)) {
            return null;
        }
        Entity owner = EntityRefJsonConverter.ReadIdentity(entityElement).Resolve();
        if (!owner.isAlive || ComponentType<T>.ID < 0 || !NativeAPI.ReadHasComponent(owner.native, ComponentType<T>.ID)) {
            return null;
        }
        return T.FromEntity(owner);
    }

    public override void Write(Utf8JsonWriter writer, T? value, JsonSerializerOptions options) {

        EntityRef identity = value != null && value.entity.isAlive
            ? NativeAPI.ReadEntityReferenceIdentity(value.entity.native)
            : EntityRef.Null;
        writer.WriteStartObject();
        writer.WritePropertyName("entity");
        EntityRefJsonConverter.WriteIdentity(writer, identity);
        writer.WriteEndObject();
    }
}

// ScriptBehaviour派生クラス <-> { "entity":{...}, "scriptSlotId":"hex", "scriptTypeId":"guid" }
// 読み込みは保存されたscriptTypeID(無ければフィールド宣言型のGUID)でnative registryから生きたinstanceを引く
internal sealed class ScriptBehaviourJsonConverterFactory : JsonConverterFactory {

    public override bool CanConvert(Type typeToConvert) {
        return typeof(ScriptBehaviour).IsAssignableFrom(typeToConvert);
    }

    public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) {
        return (JsonConverter)Activator.CreateInstance(typeof(ScriptBehaviourJsonConverter<>).MakeGenericType(typeToConvert))!;
    }
}

internal sealed class ScriptBehaviourJsonConverter<T> : JsonConverter<T> where T : ScriptBehaviour {

    public override bool HandleNull => true;

    public override T? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) {

        if (reader.TokenType == JsonTokenType.Null) {
            return null;
        }
        using JsonDocument doc = JsonDocument.ParseValue(ref reader);
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object) {
            return null;
        }

        Entity owner = root.TryGetProperty("entity", out JsonElement entityElement)
            ? EntityRefJsonConverter.ReadIdentity(entityElement).Resolve()
            : Entity.nullEntity;
        if (!owner.isAlive) {
            return null;
        }

        // 保存された型GUIDを優先し、無ければ宣言型のGUIDで引く（宣言型がabstractでも保存GUIDで解決できる）
        string typeID = root.TryGetProperty("scriptTypeId", out JsonElement t) ? (t.GetString() ?? string.Empty) : string.Empty;
        if (string.IsNullOrEmpty(typeID)) {
            typeID = HostBridge.GetScriptTypeGuid(typeof(T)) ?? string.Empty;
        }
        return HostBridge.FindScriptByGuid(owner.native, typeID) as T;
    }

    public override void Write(Utf8JsonWriter writer, T? value, JsonSerializerOptions options) {

        bool alive = value != null && value.entity.isAlive;
        EntityRef identity = alive
            ? NativeAPI.ReadEntityReferenceIdentity(value!.entity.native)
            : EntityRef.Null;
        writer.WriteStartObject();
        writer.WritePropertyName("entity");
        EntityRefJsonConverter.WriteIdentity(writer, identity);
        writer.WriteString("scriptSlotId", alive && value!.scriptSlotID != 0 ? new UUID(value.scriptSlotID).ToString() : string.Empty);
        writer.WriteString("scriptTypeId", alive ? HostBridge.GetScriptTypeGuid(value!.GetType()) ?? string.Empty : string.Empty);
        writer.WriteEndObject();
    }
}
