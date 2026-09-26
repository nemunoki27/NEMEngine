using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static class SerializationFieldTests {

    [Serializable]
    private class BaseData {
        [SerializeField] private int inherited = 7;
        internal int Inherited => inherited;
    }

    [Serializable]
    private sealed class Node {
        public int value = 19;
    }

    [Serializable]
    private sealed class RenamedData {
        [FormerlySerializedAs("oldValue"), FormerlySerializedAs("firstValue")]
        public int value = 3;
        public int[,] unsupported = new int[0, 0];
    }

    [Serializable]
    private sealed class AmbiguousData {
        [FormerlySerializedAs("oldValue")] public int first = 1;
        [FormerlySerializedAs("oldValue")] public int second = 2;
    }

    [Serializable]
    private struct ValueData {
        public int number;
    }

    [Serializable]
    private sealed class ConstructorData {
        public int number;
        public ConstructorData(int number) { this.number = number; }
    }

    [Serializable]
    private sealed class Data : BaseData {
        public int value = 11;
        [NonSerialized] public int excluded = 13;
        public readonly int immutable = 17;
        public Node first = new();
        public Node second;
        public int Property { get => throw new InvalidOperationException("property was serialized"); set { } }
        public Data() { second = first; }
    }

    [Serializable]
    private class ReferenceNode {
        public int number = 5;
        [SerializeReference] public ReferenceNode? next;
    }

    [Serializable]
    private sealed class DerivedNode : ReferenceNode {
        public int extra = 9;
    }

    private sealed class ReferenceFixture : MonoBehaviour {
        [SerializeReference] public ReferenceNode? first;
        [SerializeReference] public ReferenceNode? second;
    }

    [Serializable]
    private sealed class CallbackNode : ISerializationCallbackReceiver {
        public int number;
        [NonSerialized] public int before;
        [NonSerialized] public int after;
        public void OnBeforeSerialize() { ++before; number = 12; }
        public void OnAfterDeserialize() { ++after; }
    }

    private sealed class CallbackFixture : MonoBehaviour, ISerializationCallbackReceiver {
        [SerializeReference] public CallbackNode first = new();
        [SerializeReference] public CallbackNode second;
        [NonSerialized] public bool referencesReady;
        [NonSerialized] public int before;
        [NonSerialized] public int after;
        public CallbackFixture() { second = first; }
        public void OnBeforeSerialize() { ++before; }
        public void OnAfterDeserialize() {
            ++after;
            referencesReady = ReferenceEquals(first, second) && first.number == 12 && first.after == 0;
        }
    }

    private sealed class ThrowingCallback : MonoBehaviour, ISerializationCallbackReceiver {
        internal bool awake;
        private void Awake() { awake = true; }
        public void OnBeforeSerialize() { }
        public void OnAfterDeserialize() { throw new InvalidOperationException("callback fixture"); }
    }

    private sealed class Fixture : MonoBehaviour {
        public Data data = new();
    }

    private sealed class MovementFixture : MonoBehaviour {
        [SerializeField] private float moveSpeed = 0.0f;
        internal float Speed => moveSpeed;
    }

    private sealed class ReloadFixture : MonoBehaviour {
        private int counter = 17;
        private CallbackNode callback = new();
        private List<int> numbers = new() { 3, 7 };
        [NonSerialized] private int excluded = 5;
        private readonly int immutable = 11;
        private static int global = 13;
        private Task pending = Task.CompletedTask;
        internal int Current => counter + excluded + immutable + global + callback.number + numbers.Count + (pending.IsCompleted ? 1 : 0);
    }

    internal static void Run() {
        CheckPendingReentry();
        TestNestedRename();
        TestNestedPreservation();
        TestReloadCapture();
        TestConstructorData();
        TestInitialEntityScript();

        var codec = new ScriptFieldCodec(new ScriptTypeRegistry());
        var original = new Fixture();
        FieldInfo field = typeof(Fixture).GetField(nameof(Fixture.data))!;
        string json = codec.SerializeFieldDefault(field, original);
        JsonNode node = JsonNode.Parse(json)!;
        Check((int)node["value"]! == 11 && (int)node["inherited"]! == 7);
        Check(node["excluded"] is null && node["immutable"] is null && node["Property"] is null);

        // 通常保存の共有classは、読込後に別個体となる
        var restored = new Fixture();
        using JsonDocument document = JsonDocument.Parse(json);
        codec.SetFieldFromElement(restored, field, document.RootElement);
        Check(restored.data.Inherited == 7 && restored.data.value == 11);
        Check(restored.data.first.value == 19 && restored.data.second.value == 19);
        Check(!ReferenceEquals(restored.data.first, restored.data.second));
        TestRegistryPublication();
        TestReferenceGraph();
        TestHostReferences();
        TestSerializationCallbacks();
        TestCallbackBoundary();
        TestNumericConversion();
        Console.WriteLine("[PASS] serialized fields and atomic schema registration.");
    }

    // Nativeの初期世代で生成し、保存値と実行時編集を確認する
    private static unsafe void TestInitialEntityScript() {
        const string scriptID = "2d76dd05-c78b-4323-9e43-4b58120e2f85";
        const string fieldID = "d8713ab7-18fa-4241-88b7-685a2d139b52";
        const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic;
        var session = (ManagedAssemblySession)typeof(HostBridge).GetField("session", flags)!.GetValue(null)!;
        var instances = (ScriptInstanceStore)typeof(HostBridge).GetField("instances", flags)!.GetValue(null)!;
        var entry = new ScriptTypeEntry { type = typeof(MovementFixture), callbacks = new ScriptCallbacks(typeof(MovementFixture)) };
        FieldInfo field = typeof(MovementFixture).GetField("moveSpeed", BindingFlags.Instance | BindingFlags.NonPublic)!;
        entry.fieldMap.Add(fieldID, field);
        entry.runtimeFieldMap.Add(fieldID, field);
        session.registry.guidToEntry.Add(scriptID, entry);
        session.registry.typeToEntry.Add(entry.type, entry);
        var previousAlive = NativeAPI.IsAlive;
        NativeAPI.IsAlive = &IsInitialEntityAlive;
        NativeScriptInstanceHandle handle = NativeScriptInstanceHandle.Null;
        try {
            byte[] typeBytes = Encoding.UTF8.GetBytes(scriptID + "\0");
            byte[] fieldBytes = Encoding.UTF8.GetBytes(fieldID + "\0");
            byte[] savedBytes = Encoding.UTF8.GetBytes("{\"" + fieldID + "\":4.800000190734863}\0");
            byte[] editedBytes = Encoding.UTF8.GetBytes("7.25\0");
            var owner = new NativeEntity { world = new ManagedWorldHandle { index = 2, generation = 1 }, index = 0, generation = 0 };
            delegate* unmanaged[Cdecl]<byte*, NativeEntity, byte*, ulong, NativeScriptInstanceHandle*, int> create = &HostBridge.CreateInstance;
            delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, byte*, byte*, int> edit = &HostBridge.SetRuntimeSerializedField;
            fixed (byte* typePointer = typeBytes, savedPointer = savedBytes, fieldPointer = fieldBytes, editedPointer = editedBytes) {
                Check(create(typePointer, owner, savedPointer, 1, &handle) == (int)ManagedStatus.Ok);
                Check(instances.TryResolveSlot(handle, out MonoBehaviour instance));
                var script = (MovementFixture)instance;
                Check(script.gameObject && script.Speed == 4.8f);
                Check(ReadSpeed(handle) == 4.8f);
                Check(edit(handle, fieldPointer, editedPointer) == (int)ManagedStatus.Ok && script.Speed == 7.25f);
                Check(ReadSpeed(handle) == 7.25f);
            }
            Check(GameObject.FromNative(NativeEntity.Null) is null && GameObject.FromNative(default) is null);
        } finally {
            if (instances.TryResolveSlot(handle, out MonoBehaviour script)) { session.codec.ReleaseInstance(script); }
            instances.ReleaseSlot(handle);
            NativeAPI.IsAlive = previousAlive;
            session.registry.guidToEntry.Remove(scriptID);
            session.registry.typeToEntry.Remove(entry.type);
        }

        // Inspectorと同じサイズ取得とコピーの入口から読む
        static float ReadSpeed(NativeScriptInstanceHandle handle) {
            delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, int*, int> sizeOf = &HostBridge.GetRuntimeSerializedStateSize;
            delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, byte*, int, int*, int> copy = &HostBridge.CopyRuntimeSerializedState;
            int size = 0;
            Check(sizeOf(handle, &size) == (int)ManagedStatus.Ok);
            byte[] bytes = new byte[size + 1];
            int written = 0;
            fixed (byte* buffer = bytes) {
                Check(copy(handle, buffer, bytes.Length, &written) == (int)ManagedStatus.Ok);
            }
            return (float)JsonNode.Parse(Encoding.UTF8.GetString(bytes, 0, written))![fieldID]!;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int IsInitialEntityAlive(NativeEntity owner) =>
        owner.world.index == 2 && owner.world.generation == 1 && owner.index == 0 && owner.generation == 0 ? 1 : 0;

    // 引数付きコンストラクタだけの型を通常値と共有参照で復元する
    private static void TestConstructorData() {
        var graph = new ScriptReferenceGraph(typeof(ConstructorData).Assembly, new JsonSerializerOptions());
        var source = new ConstructorData(47);
        foreach (bool reference in new[] { false, true }) {
            graph.BeginWrite();
            JsonNode saved = graph.WriteField(typeof(ConstructorData), source, reference)!;
            var restored = (ConstructorData)graph.ReadField(typeof(ConstructorData), saved, reference)!;
            Check(restored.number == 47 && !ReferenceEquals(source, restored));
        }
    }

    // Reloadだけでprivate値を取得し、通常保存へ混ぜない
    private static void TestReloadCapture() {
        var registry = new ScriptTypeRegistry { gameAssembly = typeof(ReloadFixture).Assembly };
        registry.AddScriptTypeEntry("46e4ec2f-c4a7-4d44-8326-1914311d4903", typeof(ReloadFixture), typeof(ReloadFixture).FullName!, "Reload", "", true);
        var codec = new ScriptFieldCodec(registry);
        var script = new ReloadFixture();
        Check(JsonNode.Parse(codec.BuildSavedStateJson(script))!.AsObject().Count == 0);
        JsonObject captured = JsonNode.Parse(codec.BuildReloadStateJson(script))!.AsObject();
        string prefix = "$private:" + typeof(ReloadFixture).FullName + "/";
        Check(captured.Count == 3 && (int)captured[prefix + "counter"]! == 17);
        Check((int)captured[prefix + "callback"]!["number"]! == 12 && (int)captured[prefix + "numbers"]![1]! == 7);
        Check(JsonNode.Parse(codec.BuildSavedStateJson(script))!.AsObject().Count == 0);
    }

    // 入れ子の旧名を引き継ぎ、曖昧な対応付けを拒否する
    private static void TestNestedRename() {
        var graph = new ScriptReferenceGraph(typeof(RenamedData).Assembly, new JsonSerializerOptions());
        var renamed = (RenamedData)graph.ReadField(typeof(RenamedData), JsonNode.Parse("{\"oldValue\":21}"), false)!;
        Check(renamed.value == 21);
        JsonNode source = JsonNode.Parse("{\"oldValue\":21,\"unavailable\":{\"number\":42},\"unsupported\":[[1,2]]}")!;
        renamed = (RenamedData)graph.ReadField(typeof(RenamedData), source, false)!;
        renamed.value = 27;
        graph.BeginWrite();
        JsonNode saved = graph.WriteField(typeof(RenamedData), renamed, false, source, preserve: true)!;
        Check((int)saved["value"]! == 27 && saved["oldValue"] is null);
        Check(JsonNode.DeepEquals(saved["unavailable"], source["unavailable"]));
        Check(JsonNode.DeepEquals(saved["unsupported"], source["unsupported"]) && renamed.unsupported.Length == 0);
        Check(graph.WriteField(typeof(RenamedData), renamed, false)!["unavailable"] is null);
        JsonNode replacement = graph.WriteField(typeof(RenamedData), new RenamedData { value = 33 }, false, source, preserve: true)!;
        Check((int)replacement["value"]! == 33 && JsonNode.DeepEquals(replacement["unavailable"], source["unavailable"]));
        var current = (RenamedData)graph.ReadField(typeof(RenamedData), JsonNode.Parse("{\"value\":8,\"oldValue\":21}"), false)!;
        Check(current.value == 8);
        foreach ((Type type, string json) in new[] {
            (typeof(RenamedData), "{\"oldValue\":21,\"firstValue\":9}"),
            (typeof(AmbiguousData), "{\"oldValue\":21}"),
        }) {
            bool failed = false;
            try { graph.ReadField(type, JsonNode.Parse(json), false); }
            catch (JsonException) { failed = true; }
            Check(failed);
        }
    }

    // 未知の構造体メンバと共有参照の定義を保存する
    private static void TestNestedPreservation() {
        var options = new JsonSerializerOptions();
        JsonNode source = JsonNode.Parse("{\"number\":4,\"unavailable\":8}")!;
        var graph = new ScriptReferenceGraph(typeof(ValueData).Assembly, options);
        var value = (ValueData)graph.ReadField(typeof(ValueData), source, false)!;
        value.number = 6;
        JsonNode saved = graph.WriteField(typeof(ValueData), value, false, source, preserve: true)!;
        Check((int)saved["number"]! == 6 && (int)saved["unavailable"]! == 8);

        var record = new JsonObject {
            ["$id"] = 1L, ["type"] = typeof(ReferenceNode).FullName!.Replace('+', '.'),
            ["value"] = new JsonObject { ["number"] = 4, ["unknown"] = new JsonObject {
                ["$id"] = 2L, ["type"] = "Missing.Type", ["value"] = new JsonObject { ["back"] = new JsonObject { ["$ref"] = 1L } },
            } },
        };
        var referenceGraph = new ScriptReferenceGraph(typeof(ReferenceNode).Assembly, options, record);
        var instance = (ReferenceNode)referenceGraph.ReadField(typeof(ReferenceNode), record, true)!;
        instance.number = 7;
        referenceGraph.BeginWrite();
        var output = new JsonObject { ["first"] = referenceGraph.WriteField(typeof(ReferenceNode), instance, true, record, preserve: true) };
        referenceGraph.CompleteWrite(output);
        Check((int)output["first"]!["value"]!["number"]! == 7);
        Check((long)output["first"]!["value"]!["unknown"]!["$ref"]! == 2);
        Check((string)output["$managedReferences"]![0]!["type"]! == "Missing.Type");
        // 再読込でも定義の重複や参照切れを起こさない
        var restored = new ScriptReferenceGraph(typeof(ReferenceNode).Assembly, options, output);
        Check(((ReferenceNode)restored.ReadField(typeof(ReferenceNode), output["first"], true)!).number == 7);
    }

    // 整数の全値域と浮動小数点の丸めを区別する
    private static void TestNumericConversion() {
        object? Read(Type type, string json) {
            Check(ScriptNumericConversion.TryRead(type, JsonNode.Parse(json)!, out object? value));
            return value;
        }
        var options = new JsonSerializerOptions { IncludeFields = true, IgnoreReadOnlyProperties = true };
        ScriptNumericConversion.Configure(options);
        bool vectorFailed = false;
        try { JsonSerializer.Deserialize<Vector3>("{\"x\":16777217,\"y\":0,\"z\":0}", options); }
        catch (JsonException) { vectorFailed = true; }
        Check(vectorFailed);
        Check((ulong)Read(typeof(ulong), "18446744073709551615")! == ulong.MaxValue);
        Check((int)Read(typeof(int), "1e3")! == 1000);
        Check((float)Read(typeof(float), "0.1")! == 0.1f);
        Check((float)Read(typeof(float), "0.10000000149011612")! == 0.1f);
        foreach ((Type type, string json) in new[] {
            (typeof(float), "16777217"), (typeof(float), "9007199254740993"),
            (typeof(double), "9007199254740993"), (typeof(int), "1.5"), (typeof(uint), "-1"),
        }) {
            bool failed = false;
            try { Read(type, json); }
            catch (JsonException) { failed = true; }
            Check(failed);
        }
    }

    // 保存callbackの例外をNativeへステータスとして返す
    private static unsafe void TestCallbackBoundary() {
        const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic;
        var session = (ManagedAssemblySession)typeof(HostBridge).GetField("session", flags)!.GetValue(null)!;
        var instances = (ScriptInstanceStore)typeof(HostBridge).GetField("instances", flags)!.GetValue(null)!;
        var entry = new ScriptTypeEntry { type = typeof(ThrowingCallback) };
        session.registry.typeToEntry.Add(typeof(ThrowingCallback), entry);
        var script = new ThrowingCallback { callbacks = new ScriptCallbacks(typeof(ThrowingCallback)) };
        NativeScriptInstanceHandle handle = instances.AllocateSlot(script);
        try {
            session.codec.ApplySerializedFields(script, "{}");
            delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, int> invoke = &HostBridge.InvokeAwake;
            Check(invoke(handle) == (int)ManagedStatus.ScriptException && !script.awake);
        }
        finally {
            session.codec.ReleaseInstance(script);
            instances.ReleaseSlot(handle);
            session.registry.typeToEntry.Remove(typeof(ThrowingCallback));
        }
    }

    // 保存元のcallbackを参照先より先に一度だけ呼ぶ
    private static void TestSerializationCallbacks() {
        var registry = new ScriptTypeRegistry { gameAssembly = typeof(CallbackFixture).Assembly };
        registry.AddScriptTypeEntry("1a1d926d-6d4b-4f87-9784-7208e631c392", typeof(CallbackFixture),
            typeof(CallbackFixture).FullName!, "Callbacks", "", true);
        ScriptTypeEntry entry = registry.typeToEntry[typeof(CallbackFixture)];
        foreach (string name in new[] { "first", "second" }) {
            entry.fieldMap.Add(name, typeof(CallbackFixture).GetField(name)!);
            entry.deferredFields.Add(name);
        }
        var codec = new ScriptFieldCodec(registry);
        var source = new CallbackFixture();
        string json = codec.BuildSavedStateJson(source);
        Check(source.before == 1 && source.first.before == 1);
        var restored = new CallbackFixture();
        codec.ApplySerializedFields(restored, json);
        Check(restored.after == 0);
        codec.FlushPendingReferenceFields();
        Check(restored.after == 1 && restored.first.after == 1 && restored.referencesReady);
        codec.FlushPendingReferenceFields();
        Check(restored.after == 1 && restored.first.after == 1);
        codec.BuildRuntimeStateJson(restored);
        Check(restored.before == 0);
    }

    // Fieldをまたぐ参照を保存と明示編集の両方で維持する
    private static void TestHostReferences() {
        var registry = new ScriptTypeRegistry { gameAssembly = typeof(ReferenceFixture).Assembly };
        registry.AddScriptTypeEntry("b12c1df9-4f64-430b-aa92-61686041090f", typeof(ReferenceFixture),
            typeof(ReferenceFixture).FullName!, "References", "", true);
        ScriptTypeEntry entry = registry.typeToEntry[typeof(ReferenceFixture)];
        foreach (string name in new[] { "first", "second" }) {
            FieldInfo field = typeof(ReferenceFixture).GetField(name)!;
            entry.fieldMap.Add(name, field);
            entry.runtimeFieldMap.Add(name, field);
            entry.deferredFields.Add(name);
        }
        var codec = new ScriptFieldCodec(registry);
        var original = new ReferenceFixture { first = new DerivedNode() };
        original.second = original.first;
        original.first.next = original.first;
        string json = codec.BuildSavedStateJson(original);
        var restored = new ReferenceFixture();
        codec.ApplySerializedFields(restored, json);
        codec.FlushPendingReferenceFields();
        Check(ReferenceEquals(restored.first, restored.second) && ReferenceEquals(restored.first, restored.first!.next));
        JsonNode changed = JsonNode.Parse(json)!["first"]!.DeepClone();
        changed["value"]!["number"] = 28;
        codec.ApplyFieldValue(restored, entry.fieldMap["first"], changed.ToJsonString());
        Check(restored.first!.number == 28 && ReferenceEquals(restored.first, restored.second));
        codec.ApplyFieldValue(restored, entry.fieldMap["first"], "null");
        Check(restored.first is null && restored.second!.number == 28);
        var detached = new ReferenceFixture();
        codec.ApplySerializedFields(detached, codec.BuildSavedStateJson(restored));
        codec.FlushPendingReferenceFields();
        Check(detached.first is null && detached.second!.number == 28 && ReferenceEquals(detached.second, detached.second.next));

        // 型欠損の保存値をnullに置き換えず残す
        JsonNode missing = JsonNode.Parse(json)!;
        missing["first"]!["type"] = "Missing.ReferenceType";
        var unavailable = new ReferenceFixture();
        codec.ApplySerializedFields(unavailable, missing.ToJsonString());
        string retained = codec.BuildSavedStateJson(unavailable);
        Check(JsonNode.DeepEquals(missing, JsonNode.Parse(retained)));
        Check(unavailable.first is null && unavailable.second is null);
    }

    // 保存元内の共有と循環を保持し、別の保存元と混ぜない
    private static void TestReferenceGraph() {
        var options = new JsonSerializerOptions { IncludeFields = true };
        var first = new DerivedNode();
        var second = new ReferenceNode();
        first.next = second;
        second.next = first;
        var writer = new ScriptReferenceGraph(typeof(Fixture).Assembly, options);
        writer.BeginWrite();
        var source = new JsonObject {
            ["first"] = writer.WriteField(typeof(ReferenceNode), first, true),
            ["second"] = writer.WriteField(typeof(ReferenceNode), second, true),
            ["empty"] = writer.WriteField(typeof(ReferenceNode), null, true),
        };
        var reader = new ScriptReferenceGraph(typeof(Fixture).Assembly, options, source);
        // 定義より先に参照だけのFieldを読む
        var restoredSecond = (ReferenceNode)reader.ReadField(typeof(ReferenceNode), source["second"], true)!;
        var restoredFirst = (DerivedNode)reader.ReadField(typeof(ReferenceNode), source["first"], true)!;
        Check(ReferenceEquals(restoredFirst.next, restoredSecond));
        Check(ReferenceEquals(restoredSecond.next, restoredFirst) && restoredFirst.extra == 9);
        Check(reader.ReadField(typeof(ReferenceNode), source["empty"], true) is null);
        var other = new ScriptReferenceGraph(typeof(Fixture).Assembly, options, source);
        Check(!ReferenceEquals(restoredFirst, other.ReadField(typeof(ReferenceNode), source["first"], true)));

        // 通常保存の循環は黙って参照保存へ変えない
        bool failed = false;
        var invalid = new JsonObject { ["$ref"] = 999L };
        try { reader.ReadField(typeof(ReferenceNode), invalid, true); }
        catch (JsonException) { failed = true; }
        Check(failed);
        var missing = source.DeepClone().AsObject();
        missing["first"]!["type"] = "Missing.ReferenceType";
        failed = false;
        try { new ScriptReferenceGraph(typeof(Fixture).Assembly, options, missing)
            .ReadField(typeof(ReferenceNode), missing["first"], true); }
        catch (JsonException) { failed = true; }
        Check(failed);
    }

    // 途中失敗で公開済みの型情報を置き換えない
    private static void TestRegistryPublication() {

        const string scriptID = "47fb0a32-898f-4f2b-8e44-f37ba208c41c";
        const string fieldID = "108011d5-40c1-44de-8992-dda1ce8cd82a";
        Type type = typeof(Fixture);
        var descriptors = new[] { new ScriptTypeDescriptor(scriptID, type.FullName!, "Fixture", "", true) };
        var schema = new JsonObject {
            [scriptID] = new JsonObject {
                ["fields"] = new JsonArray(new JsonObject {
                    ["fieldId"] = fieldID, ["name"] = "data", ["declaringType"] = type.FullName,
                }),
            },
        };
        var registry = new ScriptTypeRegistry();
        var codec = new ScriptFieldCodec(registry);
        registry.RegisterScriptTypes(type.Assembly, descriptors, schema, codec);
        ScriptTypeEntry published = registry.guidToEntry[scriptID];
        Check(published.fieldMap[fieldID].Name == "data");
        Check(schema[scriptID]!["fields"]![0]!["defaultValueJson"] is null);

        // 重複IDと欠損Fieldを含む候補は全体を拒否する
        ExpectFailure(() => registry.RegisterScriptTypes(type.Assembly,
            new[] { descriptors[0], descriptors[0] }, schema, codec));
        var broken = schema.DeepClone().AsObject();
        broken[scriptID]!["fields"]![0]!["name"] = "missing";
        ExpectFailure(() => registry.RegisterScriptTypes(type.Assembly, descriptors, broken, codec));
        broken[scriptID]!["fields"] = null;
        ExpectFailure(() => registry.RegisterScriptTypes(type.Assembly, descriptors, broken, codec));
        Check(ReferenceEquals(published, registry.guidToEntry[scriptID]));
        Check(registry.scriptTypeEntries.Count == 1 && registry.typeToEntry.Count == 1);

        // 解放時に旧Scriptへ向いた保留値を捨てる
        var fixture = new Fixture();
        using JsonDocument pending = JsonDocument.Parse("{\"value\":99}");
        codec.pendingReferences.Add(fixture, published.fieldMap[fieldID], pending.RootElement);
        codec.ResetAssemblyState();
        codec.FlushPendingReferenceFields();
        Check(fixture.data.value == 11);

        // 変換不能な既存値と未登録Fieldを保存し直しても失わない
        string source = new JsonObject { [fieldID] = "incompatible", ["removed-field"] = 71 }.ToJsonString();
        codec.ApplySerializedFields(fixture, source);
        JsonNode saved = JsonNode.Parse(codec.BuildSavedStateJson(fixture))!;
        Check((string)saved[fieldID]! == "incompatible" && (int)saved["removed-field"]! == 71);
        Check(fixture.data.value == 11);
        bool failed = false;
        try { codec.ApplyFieldValue(fixture, published.fieldMap[fieldID], "42"); }
        catch (JsonException) { failed = true; }
        Check(failed && fixture.data.value == 11);

        // 明示編集の成功後は新しい値を保存する
        codec.ApplyFieldValue(fixture, published.fieldMap[fieldID], "{\"value\":37}");
        saved = JsonNode.Parse(codec.BuildSavedStateJson(fixture))!;
        Check((int)saved[fieldID]!["value"]! == 37 && (int)saved["removed-field"]! == 71);
    }

    // 適用中の取消・追加・例外で未処理の参照を失わない
    private static void CheckPendingReentry() {
        var first = new ReferenceFixture();
        var removed = new ReferenceFixture();
        FieldInfo field = typeof(ReferenceFixture).GetField(nameof(ReferenceFixture.first))!;
        using var value = JsonDocument.Parse("null");
        var calls = new List<MonoBehaviour>();
        PendingScriptReferences pending = null!;
        pending = new PendingScriptReferences((script, _, _) => {
            calls.Add(script);
            if (ReferenceEquals(script, first)) {
                pending.Remove(removed);
                pending.Add(removed, field, value.RootElement);
                pending.Flush();
            }
        });
        pending.Add(first, field, value.RootElement);
        pending.Add(removed, field, value.RootElement);
        pending.Flush();
        Check(calls.Count == 1 && ReferenceEquals(calls[0], first));
        pending.Flush();
        Check(calls.Count == 2 && ReferenceEquals(calls[1], removed));

        int attempts = 0;
        pending = new PendingScriptReferences((_, _, _) => {
            if (++attempts == 1) { throw new InvalidOperationException("fixture"); }
        });
        pending.Add(first, field, value.RootElement);
        pending.Add(removed, field, value.RootElement);
        ExpectFailure(pending.Flush);
        pending.Flush();
        Check(attempts == 2);
    }

    private static void ExpectFailure(Action action) {
        try { action(); }
        catch (InvalidOperationException) { return; }
        throw new InvalidOperationException("Invalid schema was published.");
    }

    private static void Check(bool result) {
        if (!result) { throw new InvalidOperationException("Serialized field contract failed."); }
    }
}
