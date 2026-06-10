using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace NEMEngine;

// C++側 ManagedAbi と一致させるABI定数
internal static class ManagedAbi {

    // C++側 kManagedAbiVersion と一致させる
    // v2: managed script instance handle を int32 から NativeScriptInstanceHandle へ変更
    // v3: 型登録を CopyScriptTypeInfo(Stable GUID) へ変更し、GenerateScriptManifest を追加
    // v4: 固定長フィールドABIを撤廃し、二段階blob schema/runtime state API へ移行
    // v5: object model(generic component access / Entity.Destroy / ScriptBehaviour.Enabled / world rotation・lossyScale)を追加
    // v6: 自動生成 component binding 用の typed property access(get/set + string)を追加
    // v7: gameplay API(Time拡張/TimeScale, AssetRef解決, Entity生成, Prefab/Scene, Input拡張, Audio/Animation/Application)を追加
    internal const uint Version = 7;

    // ネイティブが提供する機能カテゴリ
    internal const ulong CapabilityCore = 1ul << 0;
    internal const ulong CapabilityInput = 1ul << 1;
    internal const ulong CapabilityEntity = 1ul << 2;
    internal const ulong CapabilityHierarchy = 1ul << 3;
    internal const ulong CapabilityTransform = 1ul << 4;
    internal const ulong CapabilityObjectModel = 1ul << 5;
    internal const ulong CapabilityComponentBindings = 1ul << 6;
    internal const ulong CapabilityGameplay = 1ul << 7;

    // ScriptCoreが動作に必要とするcapability
    internal const ulong RequiredCapabilities =
        CapabilityCore | CapabilityInput | CapabilityEntity | CapabilityHierarchy | CapabilityTransform
        | CapabilityObjectModel | CapabilityComponentBindings | CapabilityGameplay;
}

// C++側 ManagedAbiHeader と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct ManagedAbiHeader {

    public uint abiVersion;
    public uint structSize;
    public ulong capabilities;
}

// C++側 ManagedScriptInstanceHandle と同一レイアウト。単純なint indexを境界で公開しない
[StructLayout(LayoutKind.Sequential)]
public readonly struct NativeScriptInstanceHandle {

    public readonly uint index;
    public readonly uint generation;

    public NativeScriptInstanceHandle(uint index, uint generation) {
        this.index = index;
        this.generation = generation;
    }

    // default(NativeScriptInstanceHandle) = {0,0} を valid と誤認しないため generation!=0 も要求する
    public bool IsValid => index != 0xffffffffu && generation != 0;
    public static NativeScriptInstanceHandle Null => new(0xffffffffu, 0);
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector3 {

    public float x;
    public float y;
    public float z;

    public static NativeVector3 From(Vector3 value) {
        return new NativeVector3 {
            x = value.x,
            y = value.y,
            z = value.z
        };
    }

    public Vector3 ToVector3() {
        return new Vector3(x, y, z);
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector2 {

    public float x;
    public float y;

    public Vector2 ToVector2() {
        return new Vector2(x, y);
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeQuaternion {

    public float x;
    public float y;
    public float z;
    public float w;

    public static NativeQuaternion From(Quaternion value) {
        return new NativeQuaternion {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w
        };
    }

    public Quaternion ToQuaternion() {
        return new Quaternion(x, y, z, w);
    }
}

internal static unsafe class NativeApi {

    private const int NameBufferSize = 256;

    internal static delegate* unmanaged[Cdecl]<float> GetDeltaTime;
    internal static delegate* unmanaged[Cdecl]<float> GetFixedDeltaTime;
    internal static delegate* unmanaged[Cdecl]<int, byte*, void> Log;
    internal static delegate* unmanaged[Cdecl]<int, int> GetKey;
    internal static delegate* unmanaged[Cdecl]<int, int> GetKeyDown;
    internal static delegate* unmanaged[Cdecl]<int, int> GetKeyUp;
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButton;
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButtonDown;
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButtonUp;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetMousePosition;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetMouseDelta;
    internal static delegate* unmanaged[Cdecl]<float> GetMouseWheel;
    internal static delegate* unmanaged[Cdecl]<int, int> GetGamepadButton;
    internal static delegate* unmanaged[Cdecl]<int, int> GetGamepadButtonDown;
    internal static delegate* unmanaged[Cdecl]<int> IsGamepadConnected;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetLeftStick;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetRightStick;
    internal static delegate* unmanaged[Cdecl]<float> GetLeftTrigger;
    internal static delegate* unmanaged[Cdecl]<float> GetRightTrigger;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> IsAlive;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> CopyName;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> SetName;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetActiveSelf;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetActiveSelf;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetActiveInHierarchy;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetParent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetFirstChild;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetNextSibling;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, void> SetParent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLocalPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetLocalPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLocalScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetLocalScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> GetLocalRotation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> SetLocalRotation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> GetRotation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> SetRotation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLossyScale;
    internal static delegate* unmanaged[Cdecl]<byte*, int> GetComponentTypeId;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int> HasComponent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> AddComponent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> RemoveComponent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> DestroyEntity;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, int> GetScriptEnabled;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, int, void> SetScriptEnabled;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> GetComponentProperty;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> SetComponentProperty;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, byte*, int, int*, int> GetComponentStringProperty;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, byte*, int, int> SetComponentStringProperty;
    // Gameplay(v7): Time 拡張 / TimeScale / AssetRef 解決
    internal static delegate* unmanaged[Cdecl]<float> GetUnscaledDeltaTime;
    internal static delegate* unmanaged[Cdecl]<float> GetUnscaledFixedDeltaTime;
    internal static delegate* unmanaged[Cdecl]<double> GetTimeSinceStartup;
    internal static delegate* unmanaged[Cdecl]<double> GetUnscaledTime;
    internal static delegate* unmanaged[Cdecl]<float> GetTimeScale;
    internal static delegate* unmanaged[Cdecl]<float, void> SetTimeScale;
    internal static delegate* unmanaged[Cdecl]<ulong> GetFrameCount;
    internal static delegate* unmanaged[Cdecl]<ulong, int> AssetExists;
    internal static delegate* unmanaged[Cdecl]<ulong, byte*, int, int> CopyAssetDisplayName;
    // Gameplay(v7): Entity 生成 / Prefab / Scene / SetParent(worldPositionStays)
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity, NativeEntity> CreateEntity;
    internal static delegate* unmanaged[Cdecl]<ulong, NativeVector3, NativeQuaternion, int, NativeEntity, NativeEntity> InstantiatePrefab;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong> LoadSceneAdditive;
    internal static delegate* unmanaged[Cdecl]<ulong, void> UnloadScene;
    internal static delegate* unmanaged[Cdecl]<ulong, int> IsSceneInstanceAlive;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, int, void> SetParentKeepWorld;
    // Gameplay(v7): raw Input 拡張（多 gamepad / axis / text / focus）
    internal static delegate* unmanaged[Cdecl]<int, int, int> GetGamepadButtonIndexed;
    internal static delegate* unmanaged[Cdecl]<int, int, int> GetGamepadButtonDownIndexed;
    internal static delegate* unmanaged[Cdecl]<int, int, int> GetGamepadButtonUpIndexed;
    internal static delegate* unmanaged[Cdecl]<int, int, float> GetGamepadAxisIndexed;
    internal static delegate* unmanaged[Cdecl]<int, int> IsGamepadConnectedIndexed;
    internal static delegate* unmanaged[Cdecl]<int> GetConnectedGamepadCount;
    internal static delegate* unmanaged[Cdecl]<int> GetHasFocus;
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> CopyTextInput;
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> CopyProjectRoot;
    // Gameplay(v7): AudioSource gameplay method
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioPlay;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioPause;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioStop;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> AudioIsPlaying;

    internal static void SetCallbacks(NativeApiTable* callbacks) {

        // C++側から渡されたECSアクセス用の関数テーブルを保持する
        GetDeltaTime = callbacks->getDeltaTime;
        GetFixedDeltaTime = callbacks->getFixedDeltaTime;
        Log = callbacks->log;
        GetKey = callbacks->getKey;
        GetKeyDown = callbacks->getKeyDown;
        GetKeyUp = callbacks->getKeyUp;
        GetMouseButton = callbacks->getMouseButton;
        GetMouseButtonDown = callbacks->getMouseButtonDown;
        GetMouseButtonUp = callbacks->getMouseButtonUp;
        GetMousePosition = callbacks->getMousePosition;
        GetMouseDelta = callbacks->getMouseDelta;
        GetMouseWheel = callbacks->getMouseWheel;
        GetGamepadButton = callbacks->getGamepadButton;
        GetGamepadButtonDown = callbacks->getGamepadButtonDown;
        IsGamepadConnected = callbacks->isGamepadConnected;
        GetLeftStick = callbacks->getLeftStick;
        GetRightStick = callbacks->getRightStick;
        GetLeftTrigger = callbacks->getLeftTrigger;
        GetRightTrigger = callbacks->getRightTrigger;
        IsAlive = callbacks->isAlive;
        CopyName = callbacks->copyName;
        SetName = callbacks->setName;
        GetActiveSelf = callbacks->getActiveSelf;
        SetActiveSelf = callbacks->setActiveSelf;
        GetActiveInHierarchy = callbacks->getActiveInHierarchy;
        GetParent = callbacks->getParent;
        GetFirstChild = callbacks->getFirstChild;
        GetNextSibling = callbacks->getNextSibling;
        SetParent = callbacks->setParent;
        GetPosition = callbacks->getPosition;
        SetPosition = callbacks->setPosition;
        GetLocalPosition = callbacks->getLocalPosition;
        SetLocalPosition = callbacks->setLocalPosition;
        GetLocalScale = callbacks->getLocalScale;
        SetLocalScale = callbacks->setLocalScale;
        GetLocalRotation = callbacks->getLocalRotation;
        SetLocalRotation = callbacks->setLocalRotation;
        GetRotation = callbacks->getRotation;
        SetRotation = callbacks->setRotation;
        GetLossyScale = callbacks->getLossyScale;
        GetComponentTypeId = callbacks->getComponentTypeId;
        HasComponent = callbacks->hasComponent;
        AddComponent = callbacks->addComponent;
        RemoveComponent = callbacks->removeComponent;
        DestroyEntity = callbacks->destroyEntity;
        GetScriptEnabled = callbacks->getScriptEnabled;
        SetScriptEnabled = callbacks->setScriptEnabled;
        GetComponentProperty = callbacks->getComponentProperty;
        SetComponentProperty = callbacks->setComponentProperty;
        GetComponentStringProperty = callbacks->getComponentStringProperty;
        SetComponentStringProperty = callbacks->setComponentStringProperty;
        GetUnscaledDeltaTime = callbacks->getUnscaledDeltaTime;
        GetUnscaledFixedDeltaTime = callbacks->getUnscaledFixedDeltaTime;
        GetTimeSinceStartup = callbacks->getTimeSinceStartup;
        GetUnscaledTime = callbacks->getUnscaledTime;
        GetTimeScale = callbacks->getTimeScale;
        SetTimeScale = callbacks->setTimeScale;
        GetFrameCount = callbacks->getFrameCount;
        AssetExists = callbacks->assetExists;
        CopyAssetDisplayName = callbacks->copyAssetDisplayName;
        CreateEntity = callbacks->createEntity;
        InstantiatePrefab = callbacks->instantiatePrefab;
        LoadSceneAdditive = callbacks->loadSceneAdditive;
        UnloadScene = callbacks->unloadScene;
        IsSceneInstanceAlive = callbacks->isSceneInstanceAlive;
        SetParentKeepWorld = callbacks->setParentKeepWorld;
        GetGamepadButtonIndexed = callbacks->getGamepadButtonIndexed;
        GetGamepadButtonDownIndexed = callbacks->getGamepadButtonDownIndexed;
        GetGamepadButtonUpIndexed = callbacks->getGamepadButtonUpIndexed;
        GetGamepadAxisIndexed = callbacks->getGamepadAxis;
        IsGamepadConnectedIndexed = callbacks->isGamepadConnectedIndexed;
        GetConnectedGamepadCount = callbacks->getConnectedGamepadCount;
        GetHasFocus = callbacks->getHasFocus;
        CopyTextInput = callbacks->copyTextInput;
        CopyProjectRoot = callbacks->copyProjectRoot;
        AudioPlay = callbacks->audioPlay;
        AudioPause = callbacks->audioPause;
        AudioStop = callbacks->audioStop;
        AudioIsPlaying = callbacks->audioIsPlaying;
    }

    internal static float ReadDeltaTime() {

        // ランタイム未初期化時はスクリプトを安全に動かさず0秒として扱う
        return GetDeltaTime != null ? GetDeltaTime() : 0.0f;
    }

    internal static float ReadFixedDeltaTime() {
        return GetFixedDeltaTime != null ? GetFixedDeltaTime() : 0.0f;
    }

    internal static void WriteLog(int level, string message) {
        if (Log == null) {
            return;
        }

        string safeMessage = message ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safeMessage) + 1];
        Encoding.UTF8.GetBytes(safeMessage, 0, safeMessage.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            Log(level, ptr);
        }
    }

    internal static bool ReadKey(int key) {
        return GetKey != null && GetKey(key) != 0;
    }

    internal static bool ReadKeyDown(int key) {
        return GetKeyDown != null && GetKeyDown(key) != 0;
    }

    internal static bool ReadKeyUp(int key) {
        return GetKeyUp != null && GetKeyUp(key) != 0;
    }

    internal static bool ReadMouseButton(int button) {
        return GetMouseButton != null && GetMouseButton(button) != 0;
    }

    internal static bool ReadMouseButtonDown(int button) {
        return GetMouseButtonDown != null && GetMouseButtonDown(button) != 0;
    }

    internal static bool ReadMouseButtonUp(int button) {
        return GetMouseButtonUp != null && GetMouseButtonUp(button) != 0;
    }

    internal static Vector2 ReadMousePosition() {
        return GetMousePosition != null ? GetMousePosition().ToVector2() : Vector2.zero;
    }

    internal static Vector2 ReadMouseDelta() {
        return GetMouseDelta != null ? GetMouseDelta().ToVector2() : Vector2.zero;
    }

    internal static float ReadMouseWheel() {
        return GetMouseWheel != null ? GetMouseWheel() : 0.0f;
    }

    internal static bool ReadGamepadButton(int button) {
        return GetGamepadButton != null && GetGamepadButton(button) != 0;
    }

    internal static bool ReadGamepadButtonDown(int button) {
        return GetGamepadButtonDown != null && GetGamepadButtonDown(button) != 0;
    }

    internal static bool ReadIsGamepadConnected() {
        return IsGamepadConnected != null && IsGamepadConnected() != 0;
    }

    internal static Vector2 ReadLeftStick() {
        return GetLeftStick != null ? GetLeftStick().ToVector2() : Vector2.zero;
    }

    internal static Vector2 ReadRightStick() {
        return GetRightStick != null ? GetRightStick().ToVector2() : Vector2.zero;
    }

    internal static float ReadLeftTrigger() {
        return GetLeftTrigger != null ? GetLeftTrigger() : 0.0f;
    }

    internal static float ReadRightTrigger() {
        return GetRightTrigger != null ? GetRightTrigger() : 0.0f;
    }

    internal static bool ReadIsAlive(NativeEntity entity) {
        return IsAlive != null && IsAlive(entity) != 0;
    }

    internal static string ReadName(NativeEntity entity) {
        if (CopyName == null) {
            return string.Empty;
        }

        byte* buffer = stackalloc byte[NameBufferSize];
        int length = CopyName(entity, buffer, NameBufferSize);
        return length <= 0 ? string.Empty : Encoding.UTF8.GetString(buffer, length);
    }

    internal static void WriteName(NativeEntity entity, string value) {
        if (SetName == null) {
            return;
        }

        string safeValue = value ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safeValue) + 1];
        Encoding.UTF8.GetBytes(safeValue, 0, safeValue.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            SetName(entity, ptr);
        }
    }

    internal static bool ReadActiveSelf(NativeEntity entity) {
        return GetActiveSelf == null || GetActiveSelf(entity) != 0;
    }

    internal static void WriteActiveSelf(NativeEntity entity, bool value) {
        if (SetActiveSelf != null) {
            SetActiveSelf(entity, value ? 1 : 0);
        }
    }

    internal static bool ReadActiveInHierarchy(NativeEntity entity) {
        return GetActiveInHierarchy != null && GetActiveInHierarchy(entity) != 0;
    }

    internal static Entity ReadParent(NativeEntity entity) {
        return new Entity(GetParent != null ? GetParent(entity) : NativeEntity.Null);
    }

    internal static Entity ReadFirstChild(NativeEntity entity) {
        return new Entity(GetFirstChild != null ? GetFirstChild(entity) : NativeEntity.Null);
    }

    internal static Entity ReadNextSibling(NativeEntity entity) {
        return new Entity(GetNextSibling != null ? GetNextSibling(entity) : NativeEntity.Null);
    }

    internal static void WriteParent(NativeEntity entity, NativeEntity parent) {
        if (SetParent != null) {
            SetParent(entity, parent);
        }
    }

    internal static Vector3 ReadPosition(NativeEntity entity) {

        // Transformの実体はC++ ECS側にあるため、C#は値だけを取得する
        return GetPosition != null ? GetPosition(entity).ToVector3() : Vector3.zero;
    }

    internal static void WritePosition(NativeEntity entity, Vector3 value) {
        if (SetPosition != null) {

            // 値の適用とdirty化はC++側でまとめて行う
            SetPosition(entity, NativeVector3.From(value));
        }
    }

    internal static Vector3 ReadLocalPosition(NativeEntity entity) {
        return GetLocalPosition != null ? GetLocalPosition(entity).ToVector3() : Vector3.zero;
    }

    internal static void WriteLocalPosition(NativeEntity entity, Vector3 value) {
        if (SetLocalPosition != null) {
            SetLocalPosition(entity, NativeVector3.From(value));
        }
    }

    internal static Vector3 ReadLocalScale(NativeEntity entity) {
        return GetLocalScale != null ? GetLocalScale(entity).ToVector3() : Vector3.one;
    }

    internal static void WriteLocalScale(NativeEntity entity, Vector3 value) {
        if (SetLocalScale != null) {
            SetLocalScale(entity, NativeVector3.From(value));
        }
    }

    internal static Quaternion ReadLocalRotation(NativeEntity entity) {
        return GetLocalRotation != null ? GetLocalRotation(entity).ToQuaternion() : Quaternion.identity;
    }

    internal static void WriteLocalRotation(NativeEntity entity, Quaternion value) {
        if (SetLocalRotation != null) {
            SetLocalRotation(entity, NativeQuaternion.From(value));
        }
    }

    internal static Quaternion ReadRotation(NativeEntity entity) {
        return GetRotation != null ? GetRotation(entity).ToQuaternion() : Quaternion.identity;
    }

    internal static void WriteRotation(NativeEntity entity, Quaternion value) {
        if (SetRotation != null) {
            SetRotation(entity, NativeQuaternion.From(value));
        }
    }

    internal static Vector3 ReadLossyScale(NativeEntity entity) {
        return GetLossyScale != null ? GetLossyScale(entity).ToVector3() : Vector3.one;
    }

    // 安定なコンポーネント名から compact な runtime type id を解決する（未登録は -1）。
    // 呼び出し側(ComponentType<T>)が type ごとに一度だけ呼んでキャッシュする
    internal static int ResolveComponentTypeId(string componentTypeName) {
        if (GetComponentTypeId == null || string.IsNullOrEmpty(componentTypeName)) {
            return -1;
        }
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(componentTypeName) + 1];
        Encoding.UTF8.GetBytes(componentTypeName, 0, componentTypeName.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            return GetComponentTypeId(ptr);
        }
    }

    internal static bool ReadHasComponent(NativeEntity entity, int typeId) {
        return HasComponent != null && typeId >= 0 && HasComponent(entity, typeId) != 0;
    }

    internal static void EnqueueAddComponent(NativeEntity entity, int typeId) {
        if (AddComponent != null && typeId >= 0) {
            AddComponent(entity, typeId);
        }
    }

    internal static void EnqueueRemoveComponent(NativeEntity entity, int typeId) {
        if (RemoveComponent != null && typeId >= 0) {
            RemoveComponent(entity, typeId);
        }
    }

    internal static void EnqueueDestroyEntity(NativeEntity entity) {
        if (DestroyEntity != null) {
            DestroyEntity(entity);
        }
    }

    // -1 = 未解決（record 無し）/ 0 / 1
    internal static int ReadScriptEnabled(NativeEntity owner, ulong scriptSlotId) {
        return GetScriptEnabled != null ? GetScriptEnabled(owner, scriptSlotId) : -1;
    }

    internal static void WriteScriptEnabled(NativeEntity owner, ulong scriptSlotId, bool enabled) {
        if (SetScriptEnabled != null) {
            SetScriptEnabled(owner, scriptSlotId, enabled ? 1 : 0);
        }
    }

    //========================================================================
    //	自動生成 component wrapper 用の typed property access（NEM.ComponentBindingGen が呼ぶ）
    //========================================================================

    // POD property を outValue へ取得する。失敗時は outValue を変更しない
    internal static void ComponentGet(NativeEntity entity, int typeId, int propertyId, void* outValue, int valueSize) {
        if (GetComponentProperty != null) {
            GetComponentProperty(entity, typeId, propertyId, outValue, valueSize);
        }
    }

    internal static void ComponentSet(NativeEntity entity, int typeId, int propertyId, void* value, int valueSize) {
        if (SetComponentProperty != null) {
            SetComponentProperty(entity, typeId, propertyId, value, valueSize);
        }
    }

    // string property を length query + buffer で取得する（固定長 buffer を使わない）
    internal static string ComponentGetString(NativeEntity entity, int typeId, int propertyId) {
        if (GetComponentStringProperty == null) {
            return string.Empty;
        }
        // まず必要 byte 数を問い合わせる（buffer=null, capacity=0 → written に必要量）
        int needed = 0;
        GetComponentStringProperty(entity, typeId, propertyId, null, 0, &needed);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed];
        int written = 0;
        fixed (byte* ptr = bytes) {
            GetComponentStringProperty(entity, typeId, propertyId, ptr, needed, &written);
        }
        return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
    }

    internal static void ComponentSetString(NativeEntity entity, int typeId, int propertyId, string value) {
        if (SetComponentStringProperty == null) {
            return;
        }
        string safe = value ?? string.Empty;
        byte[] bytes = Encoding.UTF8.GetBytes(safe);
        fixed (byte* ptr = bytes) {
            SetComponentStringProperty(entity, typeId, propertyId, ptr, bytes.Length);
        }
    }

    //========================================================================
    //	gameplay time service / AssetRef resolve helpers
    //========================================================================
    internal static float ReadUnscaledDeltaTime() => GetUnscaledDeltaTime != null ? GetUnscaledDeltaTime() : 0.0f;
    internal static float ReadUnscaledFixedDeltaTime() => GetUnscaledFixedDeltaTime != null ? GetUnscaledFixedDeltaTime() : 0.0f;
    internal static double ReadTimeSinceStartup() => GetTimeSinceStartup != null ? GetTimeSinceStartup() : 0.0;
    internal static double ReadUnscaledTime() => GetUnscaledTime != null ? GetUnscaledTime() : 0.0;
    internal static float ReadTimeScale() => GetTimeScale != null ? GetTimeScale() : 1.0f;
    internal static void WriteTimeScale(float value) { if (SetTimeScale != null) { SetTimeScale(value); } }
    internal static ulong ReadFrameCount() => GetFrameCount != null ? GetFrameCount() : 0ul;

    internal static bool ReadAssetExists(ulong assetId) => AssetExists != null && AssetExists(assetId) != 0;

    //========================================================================
    //	gameplay structural helpers（Entity 生成 / Prefab / Scene / 親子）
    //========================================================================
    // 空 Entity を即時予約して返す。name/parent は flush で適用される（pending entity）。
    internal static Entity SpawnEntity(string? name, Entity parent) {
        if (CreateEntity == null) {
            return Entity.nullEntity;
        }
        byte[] bytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + "\0");
        fixed (byte* ptr = bytes) {
            return new Entity(CreateEntity(ptr, parent.native));
        }
    }

    // 予約済みルート Entity を即時返す。実体化(component 追加)は flush で行われる。
    internal static Entity SpawnPrefab(ulong prefabAssetId, Vector3 position, Quaternion rotation, bool useTransform, Entity parent) {
        if (InstantiatePrefab == null) {
            return Entity.nullEntity;
        }
        return new Entity(InstantiatePrefab(prefabAssetId, NativeVector3.From(position),
            NativeQuaternion.From(rotation), useTransform ? 1 : 0, parent.native));
    }

    internal static ulong SceneLoadAdditive(ulong sceneAssetId) => LoadSceneAdditive != null ? LoadSceneAdditive(sceneAssetId) : 0ul;
    internal static void SceneUnload(ulong sceneInstanceId) { if (UnloadScene != null) { UnloadScene(sceneInstanceId); } }
    internal static bool SceneInstanceAlive(ulong sceneInstanceId) => IsSceneInstanceAlive != null && IsSceneInstanceAlive(sceneInstanceId) != 0;
    internal static void ReparentKeepWorld(NativeEntity child, NativeEntity parent, bool worldPositionStays) {
        if (SetParentKeepWorld != null) { SetParentKeepWorld(child, parent, worldPositionStays ? 1 : 0); }
    }

    //========================================================================
    //	AudioSource gameplay method helpers
    //========================================================================
    internal static void AudioPlayCall(NativeEntity entity) { if (AudioPlay != null) { AudioPlay(entity); } }
    internal static void AudioPauseCall(NativeEntity entity) { if (AudioPause != null) { AudioPause(entity); } }
    internal static void AudioStopCall(NativeEntity entity) { if (AudioStop != null) { AudioStop(entity); } }
    internal static bool AudioIsPlayingCall(NativeEntity entity) => AudioIsPlaying != null && AudioIsPlaying(entity) != 0;

    //========================================================================
    //	raw Input 拡張（多 gamepad / axis / text / focus）helpers
    //========================================================================
    internal static bool ReadGamepadButton(int index, int button) => GetGamepadButtonIndexed != null && GetGamepadButtonIndexed(index, button) != 0;
    internal static bool ReadGamepadButtonDown(int index, int button) => GetGamepadButtonDownIndexed != null && GetGamepadButtonDownIndexed(index, button) != 0;
    internal static bool ReadGamepadButtonUp(int index, int button) => GetGamepadButtonUpIndexed != null && GetGamepadButtonUpIndexed(index, button) != 0;
    internal static float ReadGamepadAxis(int index, int axis) => GetGamepadAxisIndexed != null ? GetGamepadAxisIndexed(index, axis) : 0.0f;
    internal static bool ReadGamepadConnected(int index) => IsGamepadConnectedIndexed != null && IsGamepadConnectedIndexed(index) != 0;
    internal static int ReadConnectedGamepadCount() => GetConnectedGamepadCount != null ? GetConnectedGamepadCount() : 0;
    internal static bool ReadHasFocus() => GetHasFocus == null || GetHasFocus() != 0;

    // project root の絶対パス（InputActions.json 等の解決用）。length-query。
    internal static string ReadProjectRoot() {
        if (CopyProjectRoot == null) {
            return string.Empty;
        }
        int needed = CopyProjectRoot(null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyProjectRoot(ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }

    // frame-local テキストを可変長で取得する（固定 buffer truncate しない）
    internal static string ReadTextInput() {
        if (CopyTextInput == null) {
            return string.Empty;
        }
        int needed = CopyTextInput(null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyTextInput(ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }

    // asset 表示名を可変長で取得する（固定 buffer で truncate しない）。length query → caller buffer。
    internal static string ReadAssetDisplayName(ulong assetId) {
        if (CopyAssetDisplayName == null || assetId == 0) {
            return string.Empty;
        }
        int needed = CopyAssetDisplayName(assetId, null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyAssetDisplayName(assetId, ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApiTable {

    // 互換性検証用ヘッダ。C++側 ManagedNativeApiTable.header と一致させる
    public ManagedAbiHeader header;

    public delegate* unmanaged[Cdecl]<float> getDeltaTime;
    public delegate* unmanaged[Cdecl]<float> getFixedDeltaTime;
    public delegate* unmanaged[Cdecl]<int, byte*, void> log;
    public delegate* unmanaged[Cdecl]<int, int> getKey;
    public delegate* unmanaged[Cdecl]<int, int> getKeyDown;
    public delegate* unmanaged[Cdecl]<int, int> getKeyUp;
    public delegate* unmanaged[Cdecl]<int, int> getMouseButton;
    public delegate* unmanaged[Cdecl]<int, int> getMouseButtonDown;
    public delegate* unmanaged[Cdecl]<int, int> getMouseButtonUp;
    public delegate* unmanaged[Cdecl]<NativeVector2> getMousePosition;
    public delegate* unmanaged[Cdecl]<NativeVector2> getMouseDelta;
    public delegate* unmanaged[Cdecl]<float> getMouseWheel;
    public delegate* unmanaged[Cdecl]<int, int> getGamepadButton;
    public delegate* unmanaged[Cdecl]<int, int> getGamepadButtonDown;
    public delegate* unmanaged[Cdecl]<int> isGamepadConnected;
    public delegate* unmanaged[Cdecl]<NativeVector2> getLeftStick;
    public delegate* unmanaged[Cdecl]<NativeVector2> getRightStick;
    public delegate* unmanaged[Cdecl]<float> getLeftTrigger;
    public delegate* unmanaged[Cdecl]<float> getRightTrigger;
    public delegate* unmanaged[Cdecl]<NativeEntity, int> isAlive;
    public delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> copyName;
    public delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> setName;
    public delegate* unmanaged[Cdecl]<NativeEntity, int> getActiveSelf;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, void> setActiveSelf;
    public delegate* unmanaged[Cdecl]<NativeEntity, int> getActiveInHierarchy;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> getParent;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> getFirstChild;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> getNextSibling;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, void> setParent;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getLocalPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setLocalPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getLocalScale;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setLocalScale;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> getLocalRotation;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> setLocalRotation;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> getRotation;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> setRotation;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getLossyScale;
    public delegate* unmanaged[Cdecl]<byte*, int> getComponentTypeId;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, int> hasComponent;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, void> addComponent;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, void> removeComponent;
    public delegate* unmanaged[Cdecl]<NativeEntity, void> destroyEntity;
    public delegate* unmanaged[Cdecl]<NativeEntity, ulong, int> getScriptEnabled;
    public delegate* unmanaged[Cdecl]<NativeEntity, ulong, int, void> setScriptEnabled;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> getComponentProperty;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> setComponentProperty;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, int, byte*, int, int*, int> getComponentStringProperty;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, int, byte*, int, int> setComponentStringProperty;
    // Gameplay(v7): Time 拡張 / TimeScale / AssetRef 解決（C++ ManagedNativeApiTable と同一順）
    public delegate* unmanaged[Cdecl]<float> getUnscaledDeltaTime;
    public delegate* unmanaged[Cdecl]<float> getUnscaledFixedDeltaTime;
    public delegate* unmanaged[Cdecl]<double> getTimeSinceStartup;
    public delegate* unmanaged[Cdecl]<double> getUnscaledTime;
    public delegate* unmanaged[Cdecl]<float> getTimeScale;
    public delegate* unmanaged[Cdecl]<float, void> setTimeScale;
    public delegate* unmanaged[Cdecl]<ulong> getFrameCount;
    public delegate* unmanaged[Cdecl]<ulong, int> assetExists;
    public delegate* unmanaged[Cdecl]<ulong, byte*, int, int> copyAssetDisplayName;
    // Gameplay(v7): Entity 生成 / Prefab / Scene / SetParent(worldPositionStays)（C++ ManagedNativeApiTable と同一順）
    public delegate* unmanaged[Cdecl]<byte*, NativeEntity, NativeEntity> createEntity;
    public delegate* unmanaged[Cdecl]<ulong, NativeVector3, NativeQuaternion, int, NativeEntity, NativeEntity> instantiatePrefab;
    public delegate* unmanaged[Cdecl]<ulong, ulong> loadSceneAdditive;
    public delegate* unmanaged[Cdecl]<ulong, void> unloadScene;
    public delegate* unmanaged[Cdecl]<ulong, int> isSceneInstanceAlive;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, int, void> setParentKeepWorld;
    // Gameplay(v7): raw Input 拡張（C++ ManagedNativeApiTable と同一順）
    public delegate* unmanaged[Cdecl]<int, int, int> getGamepadButtonIndexed;
    public delegate* unmanaged[Cdecl]<int, int, int> getGamepadButtonDownIndexed;
    public delegate* unmanaged[Cdecl]<int, int, int> getGamepadButtonUpIndexed;
    public delegate* unmanaged[Cdecl]<int, int, float> getGamepadAxis;
    public delegate* unmanaged[Cdecl]<int, int> isGamepadConnectedIndexed;
    public delegate* unmanaged[Cdecl]<int> getConnectedGamepadCount;
    public delegate* unmanaged[Cdecl]<int> getHasFocus;
    public delegate* unmanaged[Cdecl]<byte*, int, int> copyTextInput;
    public delegate* unmanaged[Cdecl]<byte*, int, int> copyProjectRoot;
    // Gameplay(v7): AudioSource gameplay method（C++ ManagedNativeApiTable と同一順）
    public delegate* unmanaged[Cdecl]<NativeEntity, void> audioPlay;
    public delegate* unmanaged[Cdecl]<NativeEntity, void> audioPause;
    public delegate* unmanaged[Cdecl]<NativeEntity, void> audioStop;
    public delegate* unmanaged[Cdecl]<NativeEntity, int> audioIsPlaying;
}
