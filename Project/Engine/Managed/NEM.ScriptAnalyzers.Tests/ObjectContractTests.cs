using System.Reflection;
using System.Text;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using NEMEngine;
using EngineObject = NEMEngine.Object;

namespace NEM.ScriptAnalyzers.Tests;

// 失効した別個体と同一参照の比較を検証する
internal static unsafe class ObjectContractTests {

    private static ulong componentInstanceID;
    private static bool gameObjectAlive = true;

    private sealed class ProbeScript : MonoBehaviour { }

    internal static void Run() {

        EngineObject first = new ProbeScript();
        EngineObject second = new ProbeScript();
        EngineObject same = first;
        int originalHash = first.GetHashCode();
        var values = new Dictionary<EngineObject, int> { [first] = 13, [second] = 27 };

        // Fake NullでもC#参照と個体の区別は失わない
        Check(first == null && second == null && first != second);
        Check(first == same && first!.Equals(same) && first.Equals(null));
        Check(!ReferenceEquals(first, null) && !(first is null) && !first);
        Check(values.Count == 2 && values[first!] == 13 && values[second!] == 27);
        Check(first!.GetHashCode() == originalHash && !first.Equals("unrelated"));

        // 同じAssetの別wrapperは同じhashで検索できる
        Texture asset = CreateTexture(new AssetGUID(1, 2));
        Texture alias = CreateTexture(new AssetGUID(1, 2));
        Texture other = CreateTexture(new AssetGUID(1, 3));
        Check(asset == alias && asset != other && asset.GetHashCode() == alias.GetHashCode());
        Check(new HashSet<EngineObject> { asset, alias, other }.Count == 2);
        CheckComponentIdentity();
        CheckScriptIdentity();
        CheckGameObjectIdentity();
        CheckMissingNativeContracts();
        CheckLongNativeStrings();
        CheckNativeStatus();
        CheckFailedAddition();
    }

    // Nativeの失敗を既定値として返さない
    // 追加に失敗した参照を呼出し元へ渡さない
    private static void CheckFailedAddition() {
        var previousAlive = NativeAPI.IsAlive;
        var previousID = NativeAPI.GetComponentInstanceID;
        var previousAdd = NativeAPI.AddComponent;
        NativeAPI.IsAlive = &ReadIsAlive;
        NativeAPI.GetComponentInstanceID = &ReadComponentInstanceID;
        NativeAPI.AddComponent = null;
        try {
            componentInstanceID = 0;
            var owner = GameObject.FromNative(new NativeEntity {
                world = new ManagedWorldHandle { index = 2, generation = 3 }, index = 5, generation = 7,
            })!;
            bool rejected = false;
            try { owner.AddComponent<Transform>(); } catch (InvalidOperationException) { rejected = true; }
            Check(rejected);
            Check(ScriptInvocationDiagnostics.Guard("fixture", () => throw new BadException()) == ManagedStatus.InternalError);
        } finally {
            NativeAPI.IsAlive = previousAlive;
            NativeAPI.GetComponentInstanceID = previousID;
            NativeAPI.AddComponent = previousAdd;
        }
    }

    private sealed class BadException : Exception {
        public override string ToString() => throw new InvalidOperationException("Cannot format exception.");
    }

    private static void CheckNativeStatus() {
        var previous = NativeAPI.GetComponentProperty;
        NativeAPI.GetComponentProperty = &FailComponentRead;
        try {
            int value = 123;
            bool failed = false;
            try { NativeAPI.ComponentGet(default, 0, 0, &value, sizeof(int)); }
            catch (InvalidOperationException) { failed = true; }
            Check(failed && value == 123);
        }
        finally { NativeAPI.GetComponentProperty = previous; }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int FailComponentRead(NativeEntity entity, int typeID, int propertyID, void* output, int size) {
        return (int)ManagedStatus.InvalidArgument;
    }

    private static readonly string longName = new string('名', 300) + "end";
    private static readonly byte[] longNameBytes = Encoding.UTF8.GetBytes(longName);

    // 長いUTF-8名と固定長の境界を確認する
    private static void CheckLongNativeStrings() {
        Check(ManagedUTF8Transfer.ReadEntityString(&CopyLongName, default) == longName);
        byte* buffer = stackalloc byte[6];
        ManagedUTF8Transfer.CopyFixed("名前", buffer, 6);
        Check(Encoding.UTF8.GetString(buffer, 3) == "名" && buffer[3] == 0);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int CopyLongName(NativeEntity entity, byte* buffer, int capacity) {
        if (buffer == null || capacity <= 0) { return longNameBytes.Length; }
        int count = System.Math.Min(longNameBytes.Length, capacity - 1);
        longNameBytes.AsSpan(0, count).CopyTo(new Span<byte>(buffer, count));
        buffer[count] = 0;
        return count;
    }

    // Native個体の再追加を模擬し、旧参照からの操作を拒否する
    private static void CheckComponentIdentity() {

        var previous = NativeAPI.GetComponentInstanceID;
        NativeAPI.GetComponentInstanceID = &ReadComponentInstanceID;
        try {
            var owner = GameObject.FromNative(new NativeEntity {
                world = new ManagedWorldHandle { index = 2, generation = 3 }, index = 5, generation = 7
            })!;
            componentInstanceID = 41;
            var first = new Transform(owner);
            var alias = new Transform(owner);
            int hash = first.GetHashCode();
            Check(first != null && first == alias && hash == alias.GetHashCode());
            componentInstanceID = 0;
            Check(first == null);
            componentInstanceID = 42;
            var replacement = new Transform(owner);
            Check(first == null && replacement != null && first != replacement && first!.GetHashCode() == hash);
            bool rejected = false;
            try {
                _ = first!.position;
            } catch (MissingReferenceException) {
                rejected = true;
            }
            Check(rejected);
            componentInstanceID = 0;
            Check(first == null && replacement == null && first != replacement);
        } finally {
            NativeAPI.GetComponentInstanceID = previous;
        }
    }

    // GameObjectを残した削除と枠の再利用を検証する
    private static void CheckScriptIdentity() {

        var previous = NativeAPI.IsAlive;
        NativeAPI.IsAlive = &ReadIsAlive;
        var store = new ScriptInstanceStore();
        try {
            var owner = GameObject.FromNative(new NativeEntity {
                world = new ManagedWorldHandle { index = 2, generation = 3 }, index = 5, generation = 7
            })!;
            var first = new ProbeScript { gameObject = owner };
            var handle = store.AllocateSlot(first);
            Check(first != null);
            bool unsubscribed = false;
            var subscription = new EventSubscription(() => unsubscribed = true);
            EventOwnerTracker.Track(first!, subscription);
            store.ReleaseSlot(handle);
            Check(first == null && unsubscribed && !subscription.IsActive);
            Check(!store.TryResolveSlot(handle, out _));

            var replacement = new ProbeScript { gameObject = owner };
            var next = store.AllocateSlot(replacement);
            Check(next.index == handle.index && next.generation != handle.generation);
            Check(replacement != null && first == null && first != replacement);
            store.ReleaseSlot(handle);
            Check(replacement != null);
            store.ReleaseAllSlots();
            Check(replacement == null && !store.TryResolveSlot(next, out _));
        } finally {
            store.ReleaseAllSlots();
            NativeAPI.IsAlive = previous;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int ReadIsAlive(NativeEntity owner) =>
        gameObjectAlive && owner.world.index == 2 && owner.world.generation == 3 && owner.index == 5 && owner.generation == 7 ? 1 : 0;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static ulong ReadComponentInstanceID(NativeEntity owner, int typeID) {

        return owner.world.index == 2 && owner.world.generation == 3 && owner.index == 5 && owner.generation == 7 &&
            typeID == Transform.componentTypeID ? componentInstanceID : 0;
    }

    // 実際のnullと破棄済み個体を区別する
    private static void CheckGameObjectIdentity() {

        var previous = NativeAPI.IsAlive;
        NativeAPI.IsAlive = &ReadIsAlive;
        try {
            Check(GameObject.FromNative(NativeEntity.Null) is null);
            var handle = new NativeEntity {
                world = new ManagedWorldHandle { index = 2, generation = 3 }, index = 5, generation = 7
            };
            GameObject first = GameObject.FromNative(handle)!;
            GameObject alias = GameObject.FromNative(handle)!;
            int hash = first.GetHashCode();
            Check(first == alias && first != null && first!.Equals(alias) && hash == alias.GetHashCode());
            gameObjectAlive = false;
            Check(first == null && !(first is null) && first!.GetHashCode() == hash);
            ++handle.generation;
            GameObject replacement = GameObject.FromNative(handle)!;
            Check(replacement == null && first != replacement);
            bool rejected = false;
            try { _ = first!.name; } catch (MissingReferenceException) { rejected = true; }
            Check(rejected);
        } finally {
            gameObjectAlive = true;
            NativeAPI.IsAlive = previous;
        }
    }

    private static void CheckMissingNativeContracts() {
        Check(GeneratedABILayout.IsValid());

        NativeAPITable table = default;
        table.header.abiVersion = ManagedAbi.Version;
        table.header.structSize = (uint)sizeof(NativeAPITable);
        table.header.capabilities = ManagedAbi.RequiredCapabilities;
        table.header.bindingFingerprint = NativeAPITable.BindingFingerprint;
        delegate* unmanaged[Cdecl]<NativeAPITable*, int> initialize = &HostBridge.InitializeNativeAPI;
        Check(initialize(&table) == (int)ManagedStatus.InvalidArgument);
        table.isAlive = &ReadIsAlive;
        Check(initialize(&table) == (int)ManagedStatus.InvalidArgument);
        table.header.bindingFingerprint ^= 1;
        Check(initialize(&table) == (int)ManagedStatus.AbiMismatch);
    }

    private static Texture CreateTexture(AssetGUID id) {

        return (Texture)Activator.CreateInstance(typeof(Texture), BindingFlags.Instance | BindingFlags.NonPublic,
            binder: null, args: new object[] { id }, culture: null)!;
    }

    private static void Check(bool result) {

        if (!result) {
            throw new InvalidOperationException("Object equality contract failed.");
        }
    }
}
