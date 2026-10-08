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
    private static ulong removedScriptSlot;
    private static int removeScriptCalls;
    private static int instantiateCalls;
    private static int instantiateUseTransform;

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
        CheckInstantiateContract();
        CheckMissingNativeContracts();
        CheckLongNativeStrings();
        CheckNullTerminatedStrings();
        CheckNativeStringTransfers();
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
            try { NativeComponentAPI.ComponentGet(default, 0, 0, &value, sizeof(int)); }
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

    private static int stringTransferMode;
    private static int stringTransferCopies;
    private static AssetGUID stringTransferAsset;
    private static NativeEntity stringTransferEntity;

    // 可変長文字列の再取得と不正サイズを確認する
    private static void CheckNativeStringTransfers() {

        var previousRoot = NativeAPI.CopyProjectRoot;
        var previousUser = NativeAPI.CopyUserSettingsRoot;
        var previousText = NativeAPI.CopyTextInput;
        var previousAsset = NativeAPI.CopyAssetDisplayName;
        var previousClip = NativeAPI.CopySkinnedAnimationCurrentClip;
        NativeAPI.CopyProjectRoot = &CopyTransferString;
        NativeAPI.CopyUserSettingsRoot = &CopyTransferString;
        NativeAPI.CopyTextInput = &CopyTransferString;
        NativeAPI.CopyAssetDisplayName = &CopyTransferAssetString;
        NativeAPI.CopySkinnedAnimationCurrentClip = &CopyTransferEntityString;
        try {
            stringTransferMode = 0;
            stringTransferCopies = 0;
            AssetGUID assetID = new(17, 29);
            NativeEntity entity = new() { index = 31, generation = 7 };
            Check(NativeApplicationAPI.ReadProjectRoot() == longName);
            Check(NativeApplicationAPI.ReadUserSettingsRoot() == longName);
            Check(NativeInputAPI.ReadTextInput() == longName);
            Check(NativeApplicationAPI.ReadAssetDisplayName(assetID) == longName && stringTransferAsset == assetID);
            Check(NativePlaybackAPI.ReadSkinnedAnimationCurrentClip(entity) == longName &&
                stringTransferEntity.index == entity.index && stringTransferEntity.generation == entity.generation);

            // 取得中に長くなっても途中の文字列を返さない
            stringTransferMode = 4;
            stringTransferCopies = 0;
            Check(ManagedUTF8Transfer.ReadString(&CopyTransferString) == longName && stringTransferCopies == 2);
            stringTransferMode = 6;
            stringTransferCopies = 0;
            Check(ManagedUTF8Transfer.ReadString(&CopyTransferString) == string.Empty);

            foreach (int mode in new[] { 1, 2, 3, 5 }) {
                stringTransferMode = mode;
                stringTransferCopies = 0;
                bool rejected = false;
                try { _ = ManagedUTF8Transfer.ReadString(&CopyTransferString); }
                catch (InvalidOperationException) { rejected = mode != 2; }
                catch (OverflowException) { rejected = mode == 2; }
                Check(rejected);
                if (mode == 5) { Check(stringTransferCopies == 3); }
            }
        }
        finally {
            NativeAPI.CopyProjectRoot = previousRoot;
            NativeAPI.CopyUserSettingsRoot = previousUser;
            NativeAPI.CopyTextInput = previousText;
            NativeAPI.CopyAssetDisplayName = previousAsset;
            NativeAPI.CopySkinnedAnimationCurrentClip = previousClip;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int CopyTransferString(byte* buffer, int capacity) => CopyTransferStringValue(buffer, capacity);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int CopyTransferAssetString(AssetGUID assetID, byte* buffer, int capacity) {
        stringTransferAsset = assetID;
        return CopyTransferStringValue(buffer, capacity);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int CopyTransferEntityString(NativeEntity entity, byte* buffer, int capacity) {
        stringTransferEntity = entity;
        return CopyTransferStringValue(buffer, capacity);
    }

    // Nativeの切り詰めと長さの変化を模擬する
    private static int CopyTransferStringValue(byte* buffer, int capacity) {
        if (buffer == null) {
            return stringTransferMode switch {
                1 => -1,
                2 => int.MaxValue,
                4 when stringTransferCopies == 0 => 3,
                5 => 1 + stringTransferCopies,
                _ => longNameBytes.Length,
            };
        }
        ++stringTransferCopies;
        if (stringTransferMode == 3) { return capacity; }
        if (stringTransferMode == 6) { buffer[0] = 0; return 0; }
        int count = stringTransferMode == 5 ? capacity - 1 : Math.Min(longNameBytes.Length, capacity - 1);
        if (stringTransferMode == 5) {
            new Span<byte>(buffer, count).Fill((byte)'x');
        } else {
            longNameBytes.AsSpan(0, count).CopyTo(new Span<byte>(buffer, count));
        }
        buffer[count] = 0;
        return count;
    }

    // null終端の容量と書込範囲を確認する
    private static void CheckNullTerminatedStrings() {

        foreach (string value in new[] { string.Empty, "名前", longName, "first\0second" }) {
            byte[] expected = Encoding.UTF8.GetBytes(value + '\0');
            byte[] actual = ManagedUTF8Transfer.GetNullTerminatedBytes(value);
            Check(actual.AsSpan().SequenceEqual(expected));
            Check(ManagedUTF8Transfer.GetNullTerminatedCapacity(value) == expected.Length);

            byte[] destination = new byte[expected.Length + 2];
            Array.Fill(destination, (byte)0x7f);
            ManagedUTF8Transfer.WriteNullTerminated(value, destination);
            Check(destination.AsSpan(0, expected.Length).SequenceEqual(expected));
            Check(destination[^1] == 0x7f && destination[^2] == 0x7f);
        }
        bool emptyRejected = false;
        try { ManagedUTF8Transfer.WriteNullTerminated("", Span<byte>.Empty); }
        catch (ArgumentException) { emptyRejected = true; }
        Check(emptyRejected);

        bool shortRejected = false;
        try { ManagedUTF8Transfer.WriteNullTerminated("名", new byte[3]); }
        catch (ArgumentException) { shortRejected = true; }
        Check(shortRejected);
    }

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
        var previousRemove = NativeAPI.RemoveScript;
        NativeAPI.IsAlive = &ReadIsAlive;
        NativeAPI.RemoveScript = &RemoveScript;
        removeScriptCalls = 0;
        var store = new ScriptInstanceStore();
        try {
            var owner = GameObject.FromNative(new NativeEntity {
                world = new ManagedWorldHandle { index = 2, generation = 3 }, index = 5, generation = 7
            })!;
            var first = new ProbeScript { gameObject = owner, scriptSlotID = 73 };
            var handle = store.AllocateSlot(first);
            Check(first != null);
            EngineObject.Destroy(first);
            Check(removedScriptSlot == 73 && removeScriptCalls == 1 && first != null);
            bool unsubscribed = false;
            var subscription = new EventSubscription(() => unsubscribed = true);
            EventOwnerTracker.Track(first!, subscription);
            store.ReleaseSlot(handle);
            // 削除済みの参照から別個体の削除を要求しない
            EngineObject.Destroy(first);
            EngineObject.Destroy(null);
            Check(removeScriptCalls == 1);
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
            NativeAPI.RemoveScript = previousRemove;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void RemoveScript(NativeEntity owner, ulong slot) {
        removedScriptSlot = slot;
        ++removeScriptCalls;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int ReadIsAlive(NativeEntity owner) =>
        gameObjectAlive && owner.world.index == 2 && owner.world.generation == 3 &&
        (owner.index == 5 || owner.index == 6) && owner.generation == 7 ? 1 : 0;

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

    // 公開InstantiateがNative複製入口とFake Nullを正しく扱う
    private static void CheckInstantiateContract() {

        var previousAlive = NativeAPI.IsAlive;
        var previousInstantiate = NativeAPI.InstantiateEntity;
        NativeAPI.IsAlive = &ReadIsAlive;
        NativeAPI.InstantiateEntity = &InstantiateEntity;
        instantiateCalls = 0;
        try {
            var source = GameObject.FromNative(new NativeEntity {
                world = new ManagedWorldHandle { index = 2, generation = 3 }, index = 5, generation = 7
            })!;
            GameObject first = EngineObject.Instantiate(source);
            Check(first != null && instantiateCalls == 1 && instantiateUseTransform == 0);
            GameObject second = EngineObject.Instantiate(source, Vector3.one, Quaternion.identity);
            Check(second != null && instantiateCalls == 2 && instantiateUseTransform == 1);

            gameObjectAlive = false;
            bool rejected = false;
            try { _ = EngineObject.Instantiate(source); } catch (ArgumentException) { rejected = true; }
            Check(rejected && instantiateCalls == 2);
        } finally {
            gameObjectAlive = true;
            NativeAPI.IsAlive = previousAlive;
            NativeAPI.InstantiateEntity = previousInstantiate;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static NativeEntity InstantiateEntity(NativeEntity source, NativeVector3 position,
        NativeQuaternion rotation, int useTransform, NativeEntity parent) {

        ++instantiateCalls;
        instantiateUseTransform = useTransform;
        source.index = 6;
        return source;
    }

    private static void CheckMissingNativeContracts() {
        Check(GeneratedABILayout.IsValid());

        NativeAPITable table = default;
        table.header.abiVersion = ManagedABI.Version;
        table.header.structSize = (uint)sizeof(NativeAPITable);
        table.header.capabilities = ManagedABI.RequiredCapabilities;
        table.header.bindingFingerprint = NativeAPITable.BindingFingerprint;
        delegate* unmanaged[Cdecl]<NativeAPITable*, int> initialize = &HostBridge.InitializeNativeAPI;
        Check(initialize(&table) == (int)ManagedStatus.InvalidArgument);
        table.isAlive = &ReadIsAlive;
        Check(initialize(&table) == (int)ManagedStatus.InvalidArgument);
        table.header.bindingFingerprint ^= 1;
        Check(initialize(&table) == (int)ManagedStatus.ABIMismatch);
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
