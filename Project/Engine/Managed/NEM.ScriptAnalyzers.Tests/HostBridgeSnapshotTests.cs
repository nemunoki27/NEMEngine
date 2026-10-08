using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static unsafe class HostBridgeSnapshotTests {

    [Serializable]
    private sealed class Value {
        public int number = 3;
    }

    private sealed class Fixture : MonoBehaviour, ISerializationCallbackReceiver {
        public int number = 17;
        [SerializeReference] public Value? reference;
        [NonSerialized] public Action? before;
        [NonSerialized] public Action? after;
        public void OnBeforeSerialize() { before?.Invoke(); }
        public void OnAfterDeserialize() { after?.Invoke(); }
    }

    // 保存callback中の解放で別個体へデータを渡さない
    internal static void Run() {

        const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic;
        const string typeID = "f6b560dc-c5ec-480e-a534-074d013a57ba";
        var session = (ManagedAssemblySession)typeof(HostBridge).GetField("session", flags)!.GetValue(null)!;
        var instances = (ScriptInstanceStore)typeof(HostBridge).GetField("instances", flags)!.GetValue(null)!;
        session.registry.AddScriptTypeEntry(typeID, typeof(Fixture), typeof(Fixture).FullName!, "Snapshot", "", true);
        ScriptTypeEntry entry = session.registry.typeToEntry[typeof(Fixture)];
        entry.fieldMap.Add("number", typeof(Fixture).GetField(nameof(Fixture.number))!);
        entry.fieldMap.Add("reference", typeof(Fixture).GetField(nameof(Fixture.reference))!);
        entry.runtimeFieldMap.Add("number", typeof(Fixture).GetField(nameof(Fixture.number))!);
        try {
            foreach (bool reload in new[] {false, true}) { CheckCapture(session, instances, reload); }
            CheckRuntimeCapture(session, instances, false);
            CheckRuntimeCapture(session, instances, true);
            CheckFieldEdit(session, instances);
        }
        finally {
            session.registry.scriptTypeEntries.Remove(entry);
            session.registry.guidToEntry.Remove(typeID);
            session.registry.typeToEntry.Remove(typeof(Fixture));
        }
        Console.WriteLine("[PASS] saved, reload and runtime snapshot callback lifetime.");
    }

    private static void CheckCapture(ManagedAssemblySession session, ScriptInstanceStore instances, bool reload) {

        delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, int*, int> capture = reload
            ? &HostBridge.GetReloadSerializedStateSize : &HostBridge.GetSavedSerializedStateSize;
        delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, byte*, int, int*, int> copy = reload
            ? &HostBridge.CopyReloadSerializedState : &HostBridge.CopySavedSerializedState;
        var fixture = new Fixture();
        var replacement = new Fixture {number = 99};
        NativeScriptInstanceHandle handle = instances.AllocateSlot(fixture);
        NativeScriptInstanceHandle replacementHandle = NativeScriptInstanceHandle.Null;
        try {
            // 確定したデータは一度だけコピーする
            int size = -1;
            Check(capture(handle, &size) == (int)ManagedStatus.Ok && size > 0);
            byte[] bytes = new byte[size + 1];
            int written = -1;
            fixed (byte* buffer = bytes) {
                // 取得後の変更と容量不足で確定値を失わない
                fixture.number = 41;
                Check(copy(handle, null, 0, &written) == (int)ManagedStatus.BufferTooSmall && written == size);
                Check(copy(handle, buffer, size - 1, &written) == (int)ManagedStatus.BufferTooSmall && written == size);
                Check(copy(handle, buffer, size, null) == (int)ManagedStatus.InvalidArgument);
                Check(copy(handle, buffer, bytes.Length, &written) == (int)ManagedStatus.Ok && written == size);
                Check((int)JsonNode.Parse(Encoding.UTF8.GetString(bytes, 0, written))!["number"]! == 17);
                Check(copy(handle, buffer, bytes.Length, &written) == (int)ManagedStatus.InvalidArgument && written == 0);
            }

            // 同じslotを再利用しても旧個体のデータを公開しない
            fixture.before = () => {
                instances.ReleaseSlot(handle);
                replacementHandle = instances.AllocateSlot(replacement);
            };
            size = -1;
            Check(capture(handle, &size) == (int)ManagedStatus.InvalidInstanceHandle && size == 0);
            Check(replacementHandle.index == handle.index && replacementHandle.generation != handle.generation);
            ScriptInstanceSlot slot = instances.slots[(int)replacementHandle.index];
            Check(slot.savedStateSnapshot is null && slot.reloadStateSnapshot is null);
        }
        finally {
            session.codec.ReleaseInstance(fixture);
            session.codec.ReleaseInstance(replacement);
            instances.ReleaseSlot(handle);
            instances.ReleaseSlot(replacementHandle);
        }
    }

    private static void Check(bool condition) {
        if (!condition) { throw new InvalidOperationException("Snapshot callback lifetime contract failed."); }
    }

    // サイズ取得と直接コピーの両方で参照解決中の解放を確認する
    private static void CheckRuntimeCapture(ManagedAssemblySession session, ScriptInstanceStore instances, bool directCopy) {

        var fixture = new Fixture();
        var replacement = new Fixture {number = 99};
        NativeScriptInstanceHandle handle = instances.AllocateSlot(fixture);
        NativeScriptInstanceHandle replacementHandle = NativeScriptInstanceHandle.Null;
        fixture.after = () => {
            session.codec.ReleaseInstance(fixture);
            instances.ReleaseSlot(handle);
            replacementHandle = instances.AllocateSlot(replacement);
        };
        try {
            session.codec.ApplySerializedFields(fixture, "{\"number\":17}");
            int size = -1;
            if (directCopy) {
                byte[] bytes = new byte[64];
                delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, byte*, int, int*, int> copy = &HostBridge.CopyRuntimeSerializedState;
                fixed (byte* buffer = bytes) {
                    Check(copy(handle, buffer, bytes.Length, &size) == (int)ManagedStatus.InvalidInstanceHandle && size == 0);
                }
            }
            else {
                delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, int*, int> capture = &HostBridge.GetRuntimeSerializedStateSize;
                Check(capture(handle, &size) == (int)ManagedStatus.InvalidInstanceHandle && size == 0);
            }
            Check(replacementHandle.index == handle.index && replacementHandle.generation != handle.generation);
            Check(instances.slots[(int)replacementHandle.index].runtimeStateSnapshot is null);
        }
        finally {
            session.codec.ReleaseInstance(fixture);
            session.codec.ReleaseInstance(replacement);
            instances.ReleaseSlot(handle);
            instances.ReleaseSlot(replacementHandle);
        }
    }

    // Field変更後に再利用したslotの取得値を失効させない
    private static void CheckFieldEdit(ManagedAssemblySession session, ScriptInstanceStore instances) {

        var fixture = new Fixture {reference = new Value()};
        var replacement = new Fixture();
        NativeScriptInstanceHandle handle = instances.AllocateSlot(fixture);
        NativeScriptInstanceHandle replacementHandle = NativeScriptInstanceHandle.Null;
        byte[] marker = new byte[] {7};
        fixture.after = () => {
            session.codec.ReleaseInstance(fixture);
            instances.ReleaseSlot(handle);
            replacementHandle = instances.AllocateSlot(replacement);
            instances.slots[(int)replacementHandle.index].runtimeStateSnapshot = marker;
        };
        try {
            session.codec.ApplySerializedFields(fixture, "{\"number\":17}");
            byte[] field = Encoding.UTF8.GetBytes("reference\0"), value = Encoding.UTF8.GetBytes("null\0");
            delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, byte*, byte*, int> edit = &HostBridge.SetRuntimeSerializedField;
            fixed (byte* fieldPointer = field, valuePointer = value) {
                Check(edit(handle, fieldPointer, valuePointer) == (int)ManagedStatus.InvalidInstanceHandle);
            }
            Check(replacementHandle.index == handle.index && replacementHandle.generation != handle.generation);
            Check(ReferenceEquals(instances.slots[(int)replacementHandle.index].runtimeStateSnapshot, marker));
        }
        finally {
            session.codec.ReleaseInstance(fixture);
            session.codec.ReleaseInstance(replacement);
            instances.ReleaseSlot(handle);
            instances.ReleaseSlot(replacementHandle);
        }
    }
}
