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
    // v8: 診断 API(reportScriptException) と script descriptor の defaultExecutionOrder を追加
    // v9: GetComponent<Script> 用に entity の script instance を scriptTypeId で引く getScriptInstance を追加
    // v10: Scene 単一load用の loadSceneSingle を追加
    // v11: EntityRef を runtime entity へ解決する resolveEntityRef を追加
    // v12: ライン描画の lineSetPoints と即時描画の lineDrawImmediate lineDrawSphereImmediate を追加
    // v13: LineRendererComponent へ1点追加する lineAddPoint を追加
    // v14: Tag公開(copyTag/setTag)とLayerマスク公開(visibility/collision typeMask)とEntity検索(byName/byTag/byComponent)を追加
    // v15: 即時形状描画の汎用 lineDrawShape を追加
    // v16: Transform 親追従の継承フラグ(ignoreParentRotation/ignoreParentScale)を追加
    // v17: 入力タイプとマウス範囲制御の get/set を追加
    // v18: MeshRenderer のマテリアル color 上書き setMeshMaterialColor を追加
    // v19: Mesh/Sprite/Text のマテリアル color の get/set(setRendererMaterialColor/getRendererMaterialColor)を追加
    // v20: Entityの保存identityを逆引きする getEntityReferenceIdentity を追加
    // v21: レイキャスト(physicsRaycast/physicsRaycastAll)とカメラレイ(screenPointToRay/getMousePositionInView)とCollisionタイプ名解決を追加
    // v22: AddComponent<Script> 用に entity へ script を runtime attach する attachScript を追加
    // v23: イージング関数 easedValue を追加、EasingType と t からイージング済みの値を返す
    // v26: UIが入力を消費したフレームのゲーム入力ブロック状態を追加
    // v27: UISelectableの決定入力配列取得と設定を追加
    // v28: UI入力配列をCanvasの上下左右と決定へ移行
    // v29: Application.Quitの終了要求を追加
    // v30: ワールド座標のGameView変換とCanvasローカル座標変換を追加
    // v32: AudioSourceのPlayOneShotとUnPauseを追加
    // v34: アセット参照を128bit AssetGUIDへ移行
    // v35: UserSettingsルート取得APIを追加
    // v36: Collision実行時状態をAuthoring設定から分離
    // v37: 型安全なDynamicBufferアクセスを追加
    // v43: 全Renderer共通の型付きMaterial Instance APIを追加
    // v44: 廃止した描画、画面遷移APIを削除
    // v45: RenderFeatureProfileの実行時パラメータAPIを追加
    // v46: ParticleSystemのUnity準拠再生操作と実行状態APIを追加
    internal const uint Version = 46;

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

// C++側 ManagedRendererMaterialTarget と一致させる
internal enum RendererMaterialTarget {

    Mesh = 0,
    Sprite,
    Text,
    Primitive,
    Line,
}

// C++側 ManagedMaterialParameterValueType と一致させる
internal enum NativeMaterialParameterValueType {

    Float = 0,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Texture,
    Int,
    UInt,
    Bool,
}

// C++側 ManagedMaterialParameterValue と同一レイアウト
[StructLayout(LayoutKind.Explicit, Size = 24)]
public struct NativeMaterialParameterValue {

    [FieldOffset(0)] internal ulong data0;
    [FieldOffset(8)] internal ulong data1;
    [FieldOffset(0)] internal float x;
    [FieldOffset(4)] internal float y;
    [FieldOffset(8)] internal float z;
    [FieldOffset(12)] internal float w;
    [FieldOffset(0)] internal int intValue;
    [FieldOffset(0)] internal uint uintValue;
    [FieldOffset(0)] internal AssetGUID assetID;
    [FieldOffset(16)] internal NativeMaterialParameterValueType type;
    [FieldOffset(20)] internal int reserved;
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

    public static NativeVector2 From(Vector2 value) {
        return new NativeVector2 {
            x = value.x,
            y = value.y
        };
    }

    public Vector2 ToVector2() {
        return new Vector2(x, y);
    }
}

// C++側 ManagedRaycastHit と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeRaycastHit {

    public NativeEntity entity;
    public NativeVector3 point;
    public NativeVector3 normal;
    public float distance;
    public int shapeIndex;
    public int trigger;
}

// C++側 ManagedSkinnedAnimationRuntimeState と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeSkinnedAnimationRuntimeState {

    public float currentTime;
    public float currentDuration;
    public float blendTime;
    public int repeatCount;
    public int initialized;
    public int finished;
    public int inTransition;
}

// C++側 ManagedUISelectableRuntimeState と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeUISelectableRuntimeState {

    public int state;
    public int normalThisFrame;
    public int selectedThisFrame;
    public int submittedThisFrame;
    public int disabledThisFrame;
}

// C++側 ManagedUIProgressRuntimeState と同一レイアウト
[StructLayout(LayoutKind.Sequential)]
public struct NativeUIProgressRuntimeState {

    public float displayedValue;
    public float delayedValue;
    public int initialized;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeColor4 {

    public float r;
    public float g;
    public float b;
    public float a;

    public static NativeColor4 From(Color4 value) {
        return new NativeColor4 {
            r = value.r,
            g = value.g,
            b = value.b,
            a = value.a
        };
    }

    public Color4 ToColor4() {
        return new Color4(r, g, b, a);
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

// 即時形状描画の種類、値は C++ ManagedLineShapeKind と一致させる
public enum LineShapeType {

    Circle2D = 0,
    Rect2D,
    Hemisphere,
    AABB,
    OBB,
    Cone,
    Arrow,
    Axis,
}

// C++側 ManagedLineShape と同一レイアウト、materialID を先頭に置き8バイト境界を揃える
[StructLayout(LayoutKind.Sequential)]
public struct NativeLineShape {

    public AssetGUID materialID;
    public int shapeType;
    public int division;
    public int is2D;
    public float radius;
    public float radius2;
    public float height;
    public float thickness;
    public NativeVector3 a;
    public NativeVector3 b;
    public NativeQuaternion rotation;
    public NativeColor4 color;
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
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int> HasComponent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> AddComponent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> RemoveComponent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> DestroyEntity;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, int> GetScriptEnabled;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, int, void> SetScriptEnabled;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, NativeScriptInstanceHandle> GetScriptInstance;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int> AttachScript;
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
    internal static delegate* unmanaged[Cdecl]<AssetGUID, int> AssetExists;
    internal static delegate* unmanaged[Cdecl]<AssetGUID, byte*, int, int> CopyAssetDisplayName;
    // Gameplay(v7): Entity 生成 / Prefab / Scene / SetParent(worldPositionStays)
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity, NativeEntity> CreateEntity;
    internal static delegate* unmanaged[Cdecl]<AssetGUID, NativeVector3, NativeQuaternion, int, NativeEntity, NativeEntity> InstantiatePrefab;
    internal static delegate* unmanaged[Cdecl]<AssetGUID, ulong> LoadSceneAdditive;
    internal static delegate* unmanaged[Cdecl]<AssetGUID, ulong> LoadSceneSingle;
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
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> CopyUserSettingsRoot;
    // Gameplay(v7): AudioSource gameplay method
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioPlay;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioPause;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioStop;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> AudioIsPlaying;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, AssetGUID, float, void> AudioPlayOneShot;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> AudioUnPause;
    // Diagnostics(v8): script callback 例外の構造化報告
    internal static delegate* unmanaged[Cdecl]<byte*, void> ReportScriptException;
    // v11: EntityRef(sourceAsset, localFileId) を runtime entity へ解決する
    internal static delegate* unmanaged[Cdecl]<AssetGUID, ulong, NativeEntity> ResolveEntityRef;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, LinePoint*, int, int, void> LineSetPoints;
    internal static delegate* unmanaged[Cdecl]<LinePoint*, int, int, int, AssetGUID, void> LineDrawImmediate;
    internal static delegate* unmanaged[Cdecl]<NativeVector3, float, NativeColor4, int, float, AssetGUID, void> LineDrawSphereImmediate;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, LinePoint, int> LineAddPoint;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, LinePoint, void> LineUpdatePoint;
    // v14: Tag / Layerマスク / Entity検索
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> CopyTag;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> SetTag;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetVisibilityLayerMask;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetVisibilityLayerMask;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetCollisionTypeMask;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetCollisionRuntimeState;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetCollisionTypeMask;
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity> FindEntityByName;
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity> FindEntityByTag;
    internal static delegate* unmanaged[Cdecl]<byte*, NativeEntity*, int, int> FindEntitiesByTag;
    internal static delegate* unmanaged[Cdecl]<int, NativeEntity> FindEntityByComponent;
    internal static delegate* unmanaged[Cdecl]<int, NativeEntity*, int, int> FindEntitiesByComponent;
    internal static delegate* unmanaged[Cdecl]<NativeLineShape*, void> LineDrawShape;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetIgnoreParentRotation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetIgnoreParentRotation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetIgnoreParentScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetIgnoreParentScale;

    // v17: 入力デバイス
    internal static delegate* unmanaged[Cdecl]<int> GetInputType;
    internal static delegate* unmanaged[Cdecl]<int, void> SetInputType;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, byte*, NativeMaterialParameterValue*, int> SetRendererMaterialParameter;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, NativeMaterialParameterValue*, int> GetRendererMaterialParameter;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, int> ClearRendererMaterialParameter;
    internal static delegate* unmanaged[Cdecl]<int> IsRayTracingSupported;
    internal static delegate* unmanaged[Cdecl]<int> IsRayTracingActive;
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> SetRenderFeaturePassEnabled;
    internal static delegate* unmanaged[Cdecl]<byte*, ulong, byte*, NativeMaterialParameterValue*, int> SetRenderFeaturePassParameter;
    internal static delegate* unmanaged[Cdecl]<byte*, ulong, int> ClearRenderFeaturePassParameter;
    internal static delegate* unmanaged[Cdecl]<byte*, int> ResetRenderFeaturePass;
    internal static delegate* unmanaged[Cdecl]<void> ResetRenderFeatureOverrides;
    internal static delegate* unmanaged[Cdecl]<int> GetMouseRangeControl;
    internal static delegate* unmanaged[Cdecl]<int, void> SetMouseRangeControl;
    // v20: Entityの保存identityを逆引きする
    internal static delegate* unmanaged[Cdecl]<NativeEntity, AssetGUID*, ulong*, int*, void> GetEntityReferenceIdentity;
    // v21: レイキャストとカメラレイとCollisionタイプ名解決
    internal static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, uint, NativeRaycastHit*, int> PhysicsRaycast;
    internal static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3, float, uint, uint, NativeRaycastHit*, int, int> PhysicsRaycastAll;
    internal static delegate* unmanaged[Cdecl]<float, float, NativeVector3*, NativeVector3*, int> ScreenPointToRay;
    internal static delegate* unmanaged[Cdecl]<NativeVector2*, int> GetMousePositionInView;
    internal static delegate* unmanaged[Cdecl]<byte*, uint> GetCollisionTypeMaskByName;
    // v23: EasingType と t からイージング済みの値を返す
    internal static delegate* unmanaged[Cdecl]<int, float, float> EasedValue;
    // Collision形状操作
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> CollisionShapeCount;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> CollisionAddShape;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> CollisionRemoveShapeAt;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, void> CollisionClearShapes;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> CollisionGetShapeProperty;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, void*, int, int> CollisionSetShapeProperty;
    // 指定クリップ名のアニメーション合計長
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, float> GetSkinnedAnimationDuration;
    // 指定クリップを頭から再生する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> PlaySkinnedAnimation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> CopySkinnedAnimationCurrentClip;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeSkinnedAnimationRuntimeState*, int> GetSkinnedAnimationRuntimeState;
    // v46: ParticleSystemの再生操作と実行状態
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int, void> ParticleSystemControl;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int> ParticleSystemState;
    // v37: POD BufferをEntityと固定Type IDから解決して操作する
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int> DynamicBufferLength;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int, void*, int, int> DynamicBufferCopy;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int, int, void*, int, int> DynamicBufferMutate;
    // v26: UI入力ブロック状態
    internal static delegate* unmanaged[Cdecl]<int> GetUIBlocksGameplayInput;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeUISelectableRuntimeState*, int> GetUISelectableRuntimeState;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeUIProgressRuntimeState*, int> GetUIProgressRuntimeState;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetCanvasInputLocked;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int> GetUIButtonClicked;
    // v28: Canvasの操作別入力配列
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int*, int, int> CanvasCopyInputBindings;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int*, int, void> CanvasSetInputBindings;
    // v29: Application終了要求
    internal static delegate* unmanaged[Cdecl]<void> RequestApplicationQuit;
    // v30: GameViewとCanvas座標変換
    internal static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3*, int> WorldToScreenPoint;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector2, NativeVector2*, int> CanvasScreenToLocalPoint;

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
        HasComponent = callbacks->hasComponent;
        AddComponent = callbacks->addComponent;
        RemoveComponent = callbacks->removeComponent;
        DestroyEntity = callbacks->destroyEntity;
        GetScriptEnabled = callbacks->getScriptEnabled;
        SetScriptEnabled = callbacks->setScriptEnabled;
        GetScriptInstance = callbacks->getScriptInstance;
        AttachScript = callbacks->attachScript;
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
        LoadSceneSingle = callbacks->loadSceneSingle;
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
        CopyUserSettingsRoot = callbacks->copyUserSettingsRoot;
        AudioPlay = callbacks->audioPlay;
        AudioPause = callbacks->audioPause;
        AudioStop = callbacks->audioStop;
        AudioIsPlaying = callbacks->audioIsPlaying;
        ReportScriptException = callbacks->reportScriptException;
        ResolveEntityRef = callbacks->resolveEntityRef;
        LineSetPoints = callbacks->lineSetPoints;
        LineDrawImmediate = callbacks->lineDrawImmediate;
        LineDrawSphereImmediate = callbacks->lineDrawSphereImmediate;
        LineAddPoint = callbacks->lineAddPoint;
        LineUpdatePoint = callbacks->lineUpdatePoint;
        CopyTag = callbacks->copyTag;
        SetTag = callbacks->setTag;
        GetVisibilityLayerMask = callbacks->getVisibilityLayerMask;
        SetVisibilityLayerMask = callbacks->setVisibilityLayerMask;
        GetCollisionTypeMask = callbacks->getCollisionTypeMask;
        GetCollisionRuntimeState = callbacks->getCollisionRuntimeState;
        SetCollisionTypeMask = callbacks->setCollisionTypeMask;
        FindEntityByName = callbacks->findEntityByName;
        FindEntityByTag = callbacks->findEntityByTag;
        FindEntitiesByTag = callbacks->findEntitiesByTag;
        FindEntityByComponent = callbacks->findEntityByComponent;
        FindEntitiesByComponent = callbacks->findEntitiesByComponent;
        LineDrawShape = callbacks->lineDrawShape;
        GetIgnoreParentRotation = callbacks->getIgnoreParentRotation;
        SetIgnoreParentRotation = callbacks->setIgnoreParentRotation;
        GetIgnoreParentScale = callbacks->getIgnoreParentScale;
        SetIgnoreParentScale = callbacks->setIgnoreParentScale;
        GetInputType = callbacks->getInputType;
        SetInputType = callbacks->setInputType;
        GetMouseRangeControl = callbacks->getMouseRangeControl;
        SetMouseRangeControl = callbacks->setMouseRangeControl;
        SetRendererMaterialParameter = callbacks->setRendererMaterialParameter;
        GetRendererMaterialParameter = callbacks->getRendererMaterialParameter;
        ClearRendererMaterialParameter = callbacks->clearRendererMaterialParameter;
        IsRayTracingSupported = callbacks->isRayTracingSupported;
        IsRayTracingActive = callbacks->isRayTracingActive;
        SetRenderFeaturePassEnabled = callbacks->setRenderFeaturePassEnabled;
        SetRenderFeaturePassParameter = callbacks->setRenderFeaturePassParameter;
        ClearRenderFeaturePassParameter = callbacks->clearRenderFeaturePassParameter;
        ResetRenderFeaturePass = callbacks->resetRenderFeaturePass;
        ResetRenderFeatureOverrides = callbacks->resetRenderFeatureOverrides;
        GetEntityReferenceIdentity = callbacks->getEntityReferenceIdentity;
        PhysicsRaycast = callbacks->physicsRaycast;
        PhysicsRaycastAll = callbacks->physicsRaycastAll;
        ScreenPointToRay = callbacks->screenPointToRay;
        GetMousePositionInView = callbacks->getMousePositionInView;
        GetCollisionTypeMaskByName = callbacks->getCollisionTypeMaskByName;
        EasedValue = callbacks->easedValue;
        CollisionShapeCount = callbacks->collisionShapeCount;
        CollisionAddShape = callbacks->collisionAddShape;
        CollisionRemoveShapeAt = callbacks->collisionRemoveShapeAt;
        CollisionClearShapes = callbacks->collisionClearShapes;
        CollisionGetShapeProperty = callbacks->collisionGetShapeProperty;
        CollisionSetShapeProperty = callbacks->collisionSetShapeProperty;
        GetSkinnedAnimationDuration = callbacks->getSkinnedAnimationDuration;
        PlaySkinnedAnimation = callbacks->playSkinnedAnimation;
        CopySkinnedAnimationCurrentClip = callbacks->copySkinnedAnimationCurrentClip;
        GetSkinnedAnimationRuntimeState = callbacks->getSkinnedAnimationRuntimeState;
        ParticleSystemControl = callbacks->particleSystemControl;
        ParticleSystemState = callbacks->particleSystemState;
        DynamicBufferLength = callbacks->dynamicBufferLength;
        DynamicBufferCopy = callbacks->dynamicBufferCopy;
        DynamicBufferMutate = callbacks->dynamicBufferMutate;
        GetUIBlocksGameplayInput = callbacks->getUIBlocksGameplayInput;
        GetUISelectableRuntimeState = callbacks->getUISelectableRuntimeState;
        GetUIProgressRuntimeState = callbacks->getUIProgressRuntimeState;
        GetCanvasInputLocked = callbacks->getCanvasInputLocked;
        GetUIButtonClicked = callbacks->getUIButtonClicked;
        CanvasCopyInputBindings = callbacks->canvasCopyInputBindings;
        CanvasSetInputBindings = callbacks->canvasSetInputBindings;
        RequestApplicationQuit = callbacks->requestApplicationQuit;
        WorldToScreenPoint = callbacks->worldToScreenPoint;
        CanvasScreenToLocalPoint = callbacks->canvasScreenToLocalPoint;
        AudioPlayOneShot = callbacks->audioPlayOneShot;
        AudioUnPause = callbacks->audioUnPause;
    }

    internal static float ReadDeltaTime() {

        // ランタイム未初期化時はスクリプトを安全に動かさず0秒として扱う
        return GetDeltaTime != null ? GetDeltaTime() : 0.0f;
    }

    internal static float ReadEasedValue(int easingType, float t) {
        // ランタイム未初期化時は補間せずそのままのtを返す
        return EasedValue != null ? EasedValue(easingType, t) : t;
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

    // script callback 例外の構造化 DTO（JSON）を native の exception store へ渡す。
    // 例外発生時にのみ呼ばれる経路なので、ここでの allocation は hot path に乗らない。
    internal static void ReportScriptExceptionJson(string json) {
        if (ReportScriptException == null) {
            return;
        }

        string safe = json ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            ReportScriptException(ptr);
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

    // 親追従で回転を無視するか。座標は常に追従するので位置の無視は持たない
    internal static bool ReadIgnoreParentRotation(NativeEntity entity) {
        return GetIgnoreParentRotation != null && GetIgnoreParentRotation(entity) != 0;
    }

    internal static void WriteIgnoreParentRotation(NativeEntity entity, bool value) {
        if (SetIgnoreParentRotation != null) {
            SetIgnoreParentRotation(entity, value ? 1 : 0);
        }
    }

    // 親追従でスケールを無視するか
    internal static bool ReadIgnoreParentScale(NativeEntity entity) {
        return GetIgnoreParentScale != null && GetIgnoreParentScale(entity) != 0;
    }

    internal static void WriteIgnoreParentScale(NativeEntity entity, bool value) {
        if (SetIgnoreParentScale != null) {
            SetIgnoreParentScale(entity, value ? 1 : 0);
        }
    }

    internal static int ReadInputType() => GetInputType != null ? GetInputType() : 0;
    internal static void WriteInputType(int type) { if (SetInputType != null) { SetInputType(type); } }
    internal static bool ReadMouseRangeControl() => GetMouseRangeControl != null && GetMouseRangeControl() != 0;
    internal static void WriteMouseRangeControl(bool enabled) { if (SetMouseRangeControl != null) { SetMouseRangeControl(enabled ? 1 : 0); } }
    // パラメータ名は保存用、IDは描画時の高速検索用として両方を境界へ渡す
    internal static bool WriteRendererMaterialParameter(
        NativeEntity entity, RendererMaterialTarget target, int subMeshIndex,
        ulong parameterID, string name, NativeMaterialParameterValue value) {

        if (SetRendererMaterialParameter == null ||
            parameterID == 0ul || string.IsNullOrEmpty(name)) {
            return false;
        }

        int byteCount = Encoding.UTF8.GetByteCount(name);
        Span<byte> bytes = byteCount < 256
            ? stackalloc byte[byteCount + 1]
            : new byte[byteCount + 1];
        Encoding.UTF8.GetBytes(name, bytes);
        bytes[byteCount] = 0;
        fixed (byte* namePtr = bytes) {
            return SetRendererMaterialParameter(
                entity, (int)target, subMeshIndex, parameterID,
                namePtr, &value) != 0;
        }
    }

    internal static bool ReadRendererMaterialParameter(
        NativeEntity entity, RendererMaterialTarget target, int subMeshIndex,
        ulong parameterID, out NativeMaterialParameterValue value) {

        NativeMaterialParameterValue result = default;
        bool succeeded = GetRendererMaterialParameter != null &&
            parameterID != 0ul &&
            GetRendererMaterialParameter(
                entity, (int)target, subMeshIndex, parameterID, &result) != 0;
        value = result;
        return succeeded;
    }

    internal static bool ClearRendererMaterialParameterValue(
        NativeEntity entity, RendererMaterialTarget target,
        int subMeshIndex, ulong parameterID) {

        return ClearRendererMaterialParameter != null &&
            parameterID != 0ul &&
            ClearRendererMaterialParameter(
                entity, (int)target, subMeshIndex, parameterID) != 0;
    }

    internal static bool ReadRayTracingSupported() =>
        IsRayTracingSupported != null && IsRayTracingSupported() != 0;

    internal static bool ReadRayTracingActive() =>
        IsRayTracingActive != null && IsRayTracingActive() != 0;

    internal static bool WriteRenderFeaturePassEnabled(
		string passName, bool enabled) {

		if (SetRenderFeaturePassEnabled == null ||
			string.IsNullOrEmpty(passName)) {
			return false;
		}
		int byteCount = Encoding.UTF8.GetByteCount(passName);
		Span<byte> bytes = byteCount < 256
			? stackalloc byte[byteCount + 1]
			: new byte[byteCount + 1];
		Encoding.UTF8.GetBytes(passName, bytes);
		bytes[byteCount] = 0;
		fixed (byte* passNamePtr = bytes) {
			return SetRenderFeaturePassEnabled(
				passNamePtr, enabled ? 1 : 0) != 0;
		}
	}

	internal static bool WriteRenderFeaturePassParameter(
		string passName, ulong parameterID, string parameterName,
		NativeMaterialParameterValue value) {

		if (SetRenderFeaturePassParameter == null || parameterID == 0ul ||
			string.IsNullOrEmpty(passName) || string.IsNullOrEmpty(parameterName)) {
			return false;
		}
		int passByteCount = Encoding.UTF8.GetByteCount(passName);
		Span<byte> passBytes = passByteCount < 256
			? stackalloc byte[passByteCount + 1]
			: new byte[passByteCount + 1];
		Encoding.UTF8.GetBytes(passName, passBytes);
		passBytes[passByteCount] = 0;

        int parameterByteCount = Encoding.UTF8.GetByteCount(parameterName);
        Span<byte> parameterBytes = parameterByteCount < 256
            ? stackalloc byte[parameterByteCount + 1]
            : new byte[parameterByteCount + 1];
        Encoding.UTF8.GetBytes(parameterName, parameterBytes);
        parameterBytes[parameterByteCount] = 0;
		fixed (byte* passNamePtr = passBytes)
		fixed (byte* parameterNamePtr = parameterBytes) {
			return SetRenderFeaturePassParameter(
				passNamePtr, parameterID, parameterNamePtr, &value) != 0;
		}
	}

	internal static bool ClearRenderFeaturePassParameterValue(
		string passName, ulong parameterID) {

		if (ClearRenderFeaturePassParameter == null || parameterID == 0ul ||
			string.IsNullOrEmpty(passName)) {
			return false;
		}
		int byteCount = Encoding.UTF8.GetByteCount(passName);
		Span<byte> bytes = byteCount < 256
			? stackalloc byte[byteCount + 1]
			: new byte[byteCount + 1];
		Encoding.UTF8.GetBytes(passName, bytes);
		bytes[byteCount] = 0;
		fixed (byte* passNamePtr = bytes) {
			return ClearRenderFeaturePassParameter(
				passNamePtr, parameterID) != 0;
		}
	}

	internal static bool ResetRenderFeaturePassValue(string passName) {

		if (ResetRenderFeaturePass == null || string.IsNullOrEmpty(passName)) {
			return false;
		}
		int byteCount = Encoding.UTF8.GetByteCount(passName);
		Span<byte> bytes = byteCount < 256
			? stackalloc byte[byteCount + 1]
			: new byte[byteCount + 1];
		Encoding.UTF8.GetBytes(passName, bytes);
		bytes[byteCount] = 0;
		fixed (byte* passNamePtr = bytes) {
			return ResetRenderFeaturePass(passNamePtr) != 0;
		}
	}

	internal static void ResetAllRenderFeatureOverrides() {

		if (ResetRenderFeatureOverrides != null) {
			ResetRenderFeatureOverrides();
		}
	}

    // Collision形状操作のマネージドラッパー、未登録時は安全な既定値を返す
    internal static int GetCollisionShapeCount(NativeEntity entity) {
        return CollisionShapeCount != null ? CollisionShapeCount(entity) : 0;
    }
    internal static void AddCollisionShape(NativeEntity entity) {
        if (CollisionAddShape != null) { CollisionAddShape(entity); }
    }
    internal static void RemoveCollisionShapeAt(NativeEntity entity, int shapeIndex) {
        if (CollisionRemoveShapeAt != null) { CollisionRemoveShapeAt(entity, shapeIndex); }
    }
    internal static void ClearCollisionShapes(NativeEntity entity) {
        if (CollisionClearShapes != null) { CollisionClearShapes(entity); }
    }
    internal static int CollisionGetShapeInt(NativeEntity entity, int shapeIndex, int propertyId) {
        int v = 0;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, shapeIndex, propertyId, &v, 4); }
        return v;
    }
    internal static void CollisionSetShapeInt(NativeEntity entity, int shapeIndex, int propertyId, int value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, shapeIndex, propertyId, &value, 4); }
    }
    internal static float CollisionGetShapeFloat(NativeEntity entity, int shapeIndex, int propertyId) {
        float v = 0.0f;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, shapeIndex, propertyId, &v, 4); }
        return v;
    }
    internal static void CollisionSetShapeFloat(NativeEntity entity, int shapeIndex, int propertyId, float value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, shapeIndex, propertyId, &value, 4); }
    }
    internal static Vector2 CollisionGetShapeVector2(NativeEntity entity, int shapeIndex, int propertyId) {
        Vector2 v = default;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, shapeIndex, propertyId, &v, 8); }
        return v;
    }
    internal static void CollisionSetShapeVector2(NativeEntity entity, int shapeIndex, int propertyId, Vector2 value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, shapeIndex, propertyId, &value, 8); }
    }
    internal static Vector3 CollisionGetShapeVector3(NativeEntity entity, int shapeIndex, int propertyId) {
        Vector3 v = default;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, shapeIndex, propertyId, &v, 12); }
        return v;
    }
    internal static void CollisionSetShapeVector3(NativeEntity entity, int shapeIndex, int propertyId, Vector3 value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, shapeIndex, propertyId, &value, 12); }
    }

    // 指定クリップ名のアニメーション合計長を返す、未登録や未ロードは0
    internal static float ReadSkinnedAnimationDuration(NativeEntity entity, string clipName) {
        if (GetSkinnedAnimationDuration == null) {
            return 0.0f;
        }
        string safe = clipName ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            return GetSkinnedAnimationDuration(entity, ptr);
        }
    }

    // 指定クリップを頭から再生する、終了フラグを同フレームで下ろす
    internal static void PlaySkinnedAnimationClip(NativeEntity entity, string clipName) {
        if (PlaySkinnedAnimation == null) {
            return;
        }
        string safe = clipName ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            PlaySkinnedAnimation(entity, ptr);
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

    // チャンク外Runtimeから現在のクリップ名を取得する
    internal static string ReadSkinnedAnimationCurrentClip(NativeEntity entity) {
        if (CopySkinnedAnimationCurrentClip == null) {
            return string.Empty;
        }

        int length = CopySkinnedAnimationCurrentClip(entity, null, 0);
        if (length <= 0) {
            return string.Empty;
        }

        byte[] bytes = new byte[length + 1];
        fixed (byte* buffer = bytes) {
            int written = CopySkinnedAnimationCurrentClip(
                entity, buffer, bytes.Length);
            return written <= 0 ? string.Empty :
                Encoding.UTF8.GetString(buffer, written);
        }
    }

    // チャンク外Runtimeから固定長の再生状態を取得する
    internal static NativeSkinnedAnimationRuntimeState ReadSkinnedAnimationRuntimeState(
        NativeEntity entity) {

        NativeSkinnedAnimationRuntimeState state = default;
        if (GetSkinnedAnimationRuntimeState != null) {
            GetSkinnedAnimationRuntimeState(entity, &state);
        }
        return state;
    }

    // UI選択の状態とフレーム遷移をまとめて取得する
    internal static NativeUISelectableRuntimeState ReadUISelectableRuntimeState(
        NativeEntity entity) {

        NativeUISelectableRuntimeState state = default;
        if (GetUISelectableRuntimeState != null) {
            GetUISelectableRuntimeState(entity, &state);
        }
        return state;
    }

    // UIProgressの補間済み表示値をまとめて取得する
    internal static NativeUIProgressRuntimeState ReadUIProgressRuntimeState(
        NativeEntity entity) {

        NativeUIProgressRuntimeState state = default;
        if (GetUIProgressRuntimeState != null) {
            GetUIProgressRuntimeState(entity, &state);
        }
        return state;
    }

    internal static int ReadDynamicBufferLength(
        NativeEntity entity, int typeId, int elementSize) {

        return DynamicBufferLength != null ?
            DynamicBufferLength(entity, typeId, elementSize) : -1;
    }

    internal static int CopyDynamicBuffer(
        NativeEntity entity, int typeId, int elementSize,
        int startIndex, void* destination, int capacity) {

        return DynamicBufferCopy != null ?
            DynamicBufferCopy(
                entity, typeId, elementSize,
                startIndex, destination, capacity) : -1;
    }

    internal static bool MutateDynamicBuffer(
        NativeEntity entity, int typeId, int elementSize,
        int operation, int index, void* data, int count) {

        return DynamicBufferMutate != null &&
            DynamicBufferMutate(
                entity, typeId, elementSize,
                operation, index, data, count) != 0;
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

    // entity 上で scriptTypeId 一致の script instance ハンドルを引く。未解決は Null
    internal static NativeScriptInstanceHandle FindScriptInstance(NativeEntity owner, string scriptTypeId) {
        if (GetScriptInstance == null || string.IsNullOrEmpty(scriptTypeId)) {
            return NativeScriptInstanceHandle.Null;
        }
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(scriptTypeId) + 1];
        Encoding.UTF8.GetBytes(scriptTypeId, 0, scriptTypeId.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            return GetScriptInstance(owner, ptr);
        }
    }

    // entity へ scriptTypeId の script を runtime attach する。生成成否を返す
    internal static bool TryAttachScript(NativeEntity owner, string scriptTypeId) {
        if (AttachScript == null || string.IsNullOrEmpty(scriptTypeId)) {
            return false;
        }
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(scriptTypeId) + 1];
        Encoding.UTF8.GetBytes(scriptTypeId, 0, scriptTypeId.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            return AttachScript(owner, ptr) != 0;
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

    internal static bool ReadAssetExists(AssetGUID assetId) =>
        AssetExists != null && AssetExists(assetId) != 0;

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
    internal static Entity SpawnPrefab(AssetGUID prefabAssetId, Vector3 position, Quaternion rotation, bool useTransform, Entity parent) {
        if (InstantiatePrefab == null) {
            return Entity.nullEntity;
        }
        return new Entity(InstantiatePrefab(prefabAssetId, NativeVector3.From(position),
            NativeQuaternion.From(rotation), useTransform ? 1 : 0, parent.native));
    }

    // EntityRefをruntime entityへ解決する、未解決はnull。結果はスクリプト側でキャッシュ推奨
    internal static Entity ResolveEntityReference(AssetGUID sourceAsset, ulong localFileId)
        => (ResolveEntityRef != null && localFileId != 0) ? new Entity(ResolveEntityRef(sourceAsset, localFileId)) : Entity.nullEntity;

    // レイキャストの最近ヒットを取得する、ヒット無しはfalse
    internal static bool RaycastClosest(Vector3 origin, Vector3 direction, float maxDistance,
        uint layerMask, uint targets, out NativeRaycastHit hit) {

        hit = default;
        if (PhysicsRaycast == null) {
            return false;
        }
        fixed (NativeRaycastHit* hitPtr = &hit) {
            return PhysicsRaycast(NativeVector3.From(origin), NativeVector3.From(direction),
                maxDistance, layerMask, targets, hitPtr) != 0;
        }
    }

    // レイキャストの全ヒットをbufferへ書き込み、ヒット総数を返す(capacity超過分は書かれない)
    internal static int RaycastMany(Vector3 origin, Vector3 direction, float maxDistance,
        uint layerMask, uint targets, Span<NativeRaycastHit> buffer) {

        if (PhysicsRaycastAll == null) {
            return 0;
        }
        fixed (NativeRaycastHit* bufferPtr = buffer) {
            return PhysicsRaycastAll(NativeVector3.From(origin), NativeVector3.From(direction),
                maxDistance, layerMask, targets, bufferPtr, buffer.Length);
        }
    }

    // GameViewピクセル座標からワールドレイを作る、カメラ未解決はfalse
    internal static bool ReadScreenPointToRay(Vector2 screenPos, out Vector3 origin, out Vector3 direction) {

        origin = Vector3.zero;
        direction = Vector3.zero;
        if (ScreenPointToRay == null) {
            return false;
        }
        NativeVector3 nativeOrigin = default;
        NativeVector3 nativeDirection = default;
        if (ScreenPointToRay(screenPos.x, screenPos.y, &nativeOrigin, &nativeDirection) == 0) {
            return false;
        }
        origin = nativeOrigin.ToVector3();
        direction = nativeDirection.ToVector3();
        return true;
    }

    // ワールド座標をGameViewピクセル座標へ変換する、カメラ未解決はfalse
    internal static bool ReadWorldToScreenPoint(Vector3 worldPosition, out Vector3 screenPosition) {

        screenPosition = Vector3.zero;
        if (WorldToScreenPoint == null) {
            return false;
        }
        NativeVector3 nativePosition = default;
        if (WorldToScreenPoint(NativeVector3.From(worldPosition), &nativePosition) == 0) {
            return false;
        }
        screenPosition = nativePosition.ToVector3();
        return true;
    }

    // GameView内のマウス座標を描画解像度基準で取得する、View外はfalse
    internal static bool ReadMousePositionInView(out Vector2 position) {

        position = Vector2.zero;
        if (GetMousePositionInView == null) {
            return false;
        }
        NativeVector2 nativePosition = default;
        if (GetMousePositionInView(&nativePosition) == 0) {
            return false;
        }
        position = nativePosition.ToVector2();
        return true;
    }

    // Collisionタイプ名からビットマスクを引く、未登録は0
    internal static uint ReadCollisionTypeMask(string name) {

        if (GetCollisionTypeMaskByName == null || string.IsNullOrEmpty(name)) {
            return 0;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(name + "\0");
        fixed (byte* ptr = bytes) {
            return GetCollisionTypeMaskByName(ptr);
        }
    }

    // Entityの保存identityを逆引きする。SceneObjectが無ければNull identity
    internal static EntityRef ReadEntityReferenceIdentity(NativeEntity entity) {
        if (GetEntityReferenceIdentity == null) {
            return EntityRef.Null;
        }
        AssetGUID sourceAsset = AssetGUID.None;
        ulong localFileId = 0;
        int kind = 0;
        GetEntityReferenceIdentity(entity, &sourceAsset, &localFileId, &kind);
        return new EntityRef((EntityRefKind)kind, sourceAsset, new UUID(localFileId));
    }

    // LineRendererComponent の点列を差し替える、count0でクリア
    internal static void LineSetComponentPoints(NativeEntity entity, ReadOnlySpan<LinePoint> points, bool loop) {
        if (LineSetPoints == null) {
            return;
        }
        fixed (LinePoint* p = points) {
            LineSetPoints(entity, p, points.Length, loop ? 1 : 0);
        }
    }

	internal static bool ReadUIBlocksGameplayInput() {
		return GetUIBlocksGameplayInput != null && GetUIBlocksGameplayInput() != 0;
	}

    // Canvasの入力配列を操作種別とデバイス別に取得する
    internal static int[] CanvasGetInputBindings(
        NativeEntity entity, int action, int device) {

        if (CanvasCopyInputBindings == null) {
            return Array.Empty<int>();
        }
        int count = CanvasCopyInputBindings(entity, action, device, null, 0);
        if (count <= 0) {
            return Array.Empty<int>();
        }

        int[] bindings = new int[count];
        int currentCount;
        fixed (int* values = bindings) {
            currentCount = CanvasCopyInputBindings(
                entity, action, device, values, count);
        }
        if (currentCount < count) {
            Array.Resize(ref bindings, Math.Max(currentCount, 0));
        }
        return bindings;
    }

    // Canvasの入力配列を操作種別とデバイス別に置き換える
    internal static void CanvasSetInputBindingsValue(
        NativeEntity entity, int action, int device, ReadOnlySpan<int> bindings) {

        if (CanvasSetInputBindings == null) {
            return;
        }
        if (bindings.Length == 0) {
            CanvasSetInputBindings(entity, action, device, null, 0);
            return;
        }
        fixed (int* values = bindings) {
            CanvasSetInputBindings(
                entity, action, device, values, bindings.Length);
        }
    }

    // スクリーン座標をCanvasローカル座標へ変換する
    internal static bool ReadCanvasScreenToLocalPoint(
        NativeEntity entity, Vector2 screenPosition, out Vector2 localPosition) {

        localPosition = Vector2.zero;
        if (CanvasScreenToLocalPoint == null) {
            return false;
        }
        NativeVector2 nativePosition = default;
        if (CanvasScreenToLocalPoint(
            entity, NativeVector2.From(screenPosition), &nativePosition) == 0) {
            return false;
        }
        localPosition = nativePosition.ToVector2();
        return true;
    }

    // LineRendererComponent の末尾へ1点追加し、追加した位置のindexを返す。失敗時は-1
    internal static int LineAddComponentPoint(NativeEntity entity, LinePoint point) {
        if (LineAddPoint == null) {
            return -1;
        }
        return LineAddPoint(entity, point);
    }

    // LineRendererComponent の point.index の点を更新する。indexが範囲外なら何もしない
    internal static void LineUpdateComponentPoint(NativeEntity entity, LinePoint point) {
        if (LineUpdatePoint == null) {
            return;
        }
        LineUpdatePoint(entity, point);
    }

    // 即時ライン描画でこのフレームだけ任意ポリラインを描く
    internal static void LineDrawImmediatePolyline(ReadOnlySpan<LinePoint> points, bool loop, bool is2D, AssetGUID materialID) {
        if (LineDrawImmediate == null || points.Length < 2) {
            return;
        }
        fixed (LinePoint* p = points) {
            LineDrawImmediate(p, points.Length, loop ? 1 : 0, is2D ? 1 : 0, materialID);
        }
    }

    // 即時球描画で組み込みの球生成を使う
    internal static void LineDrawImmediateSphere(Vector3 center, float radius, Color4 color, int division, float thickness, AssetGUID materialID) {
        if (LineDrawSphereImmediate == null) {
            return;
        }
        LineDrawSphereImmediate(NativeVector3.From(center), radius, NativeColor4.From(color), division, thickness, materialID);
    }

    // 即時形状描画、記述子1件を渡してC++側で線分へ展開する
    internal static void LineDrawShapeImmediate(NativeLineShape shape) {
        if (LineDrawShape == null) {
            return;
        }
        LineDrawShape(&shape);
    }

    internal static ulong SceneLoadAdditive(AssetGUID sceneAssetId) => LoadSceneAdditive != null ? LoadSceneAdditive(sceneAssetId) : 0ul;
    internal static ulong SceneLoadSingle(AssetGUID sceneAssetId) => LoadSceneSingle != null ? LoadSceneSingle(sceneAssetId) : 0ul;
    internal static void SceneUnload(ulong sceneInstanceId) { if (UnloadScene != null) { UnloadScene(sceneInstanceId); } }
    internal static bool SceneInstanceAlive(ulong sceneInstanceId) => IsSceneInstanceAlive != null && IsSceneInstanceAlive(sceneInstanceId) != 0;
    internal static void ReparentKeepWorld(NativeEntity child, NativeEntity parent, bool worldPositionStays) {
        if (SetParentKeepWorld != null) { SetParentKeepWorld(child, parent, worldPositionStays ? 1 : 0); }
    }

    //========================================================================
    //	AudioSource gameplay method helpers
    //========================================================================
    internal static void AudioPlayCall(NativeEntity entity) { if (AudioPlay != null) { AudioPlay(entity); } }
    internal static void AudioPlayOneShotCall(NativeEntity entity, AssetGUID clipID, float volumeScale) {
        if (AudioPlayOneShot != null) { AudioPlayOneShot(entity, clipID, volumeScale); }
    }
    internal static void AudioPauseCall(NativeEntity entity) { if (AudioPause != null) { AudioPause(entity); } }
    internal static void AudioUnPauseCall(NativeEntity entity) { if (AudioUnPause != null) { AudioUnPause(entity); } }
    internal static void AudioStopCall(NativeEntity entity) { if (AudioStop != null) { AudioStop(entity); } }
    internal static bool AudioIsPlayingCall(NativeEntity entity) => AudioIsPlaying != null && AudioIsPlaying(entity) != 0;

    //========================================================================
    //	ParticleSystem gameplay method helpers
    //========================================================================
    internal static void ParticleSystemControlCall(
        NativeEntity entity, int operation,
        ParticleSystemStopBehavior stopBehavior, bool withChildren) {

        if (ParticleSystemControl != null) {
            ParticleSystemControl(entity, operation,
                (int)stopBehavior, withChildren ? 1 : 0);
        }
    }

    internal static int ParticleSystemStateCall(
        NativeEntity entity, int state, bool withChildren = false) =>
        ParticleSystemState != null ?
            ParticleSystemState(entity, state, withChildren ? 1 : 0) : 0;

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
    internal static void RequestApplicationQuitCall() { if (RequestApplicationQuit != null) { RequestApplicationQuit(); } }

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
    internal static string ReadAssetDisplayName(AssetGUID assetId) {
        if (CopyAssetDisplayName == null || !assetId.isValid) {
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

    //========================================================================
    //	Tag / Layerマスク公開（v14）
    //========================================================================
    internal static string ReadTag(NativeEntity entity) {
        if (CopyTag == null) {
            return "Untagged";
        }
        byte* buffer = stackalloc byte[NameBufferSize];
        int length = CopyTag(entity, buffer, NameBufferSize);
        return length <= 0 ? "Untagged" : Encoding.UTF8.GetString(buffer, length);
    }

    internal static void WriteTag(NativeEntity entity, string value) {
        if (SetTag == null) {
            return;
        }
        string safe = value ?? "Untagged";
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            SetTag(entity, ptr);
        }
    }

    internal static uint ReadVisibilityLayerMask(NativeEntity entity)
        => GetVisibilityLayerMask != null ? (uint)GetVisibilityLayerMask(entity) : 0u;
    internal static void WriteVisibilityLayerMask(NativeEntity entity, uint mask) {
        if (SetVisibilityLayerMask != null) { SetVisibilityLayerMask(entity, (int)mask); }
    }
    internal static uint ReadCollisionTypeMask(NativeEntity entity)
        => GetCollisionTypeMask != null ? (uint)GetCollisionTypeMask(entity) : 0u;
    internal static bool ReadCollisionRuntimeState(NativeEntity entity)
        => GetCollisionRuntimeState != null && GetCollisionRuntimeState(entity) != 0;
    internal static void WriteCollisionTypeMask(NativeEntity entity, uint mask) {
        if (SetCollisionTypeMask != null) { SetCollisionTypeMask(entity, (int)mask); }
    }

    //========================================================================
    //	Entity検索（v14）。走査はネイティブのアクティブワールド全体に対するO(n)
    //========================================================================
    internal static Entity FindByName(string name) {
        if (FindEntityByName == null || string.IsNullOrEmpty(name)) {
            return Entity.nullEntity;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(name + "\0");
        fixed (byte* ptr = bytes) {
            return new Entity(FindEntityByName(ptr));
        }
    }

    internal static Entity FindByTag(string tag) {
        if (FindEntityByTag == null || string.IsNullOrEmpty(tag)) {
            return Entity.nullEntity;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(tag + "\0");
        fixed (byte* ptr = bytes) {
            return new Entity(FindEntityByTag(ptr));
        }
    }

    internal static Entity[] FindManyByTag(string tag) {
        if (FindEntitiesByTag == null || string.IsNullOrEmpty(tag)) {
            return System.Array.Empty<Entity>();
        }
        byte[] bytes = Encoding.UTF8.GetBytes(tag + "\0");
        fixed (byte* ptr = bytes) {
            // length-query で総数を得てから確保し、再取得して詰める
            int count = FindEntitiesByTag(ptr, null, 0);
            if (count <= 0) {
                return System.Array.Empty<Entity>();
            }
            var buffer = new NativeEntity[count];
            fixed (NativeEntity* bp = buffer) {
                int written = FindEntitiesByTag(ptr, bp, count);
                return MakeEntityArray(buffer, written < count ? written : count);
            }
        }
    }

    internal static Entity FindByComponent(int typeId) {
        if (FindEntityByComponent == null || typeId < 0) {
            return Entity.nullEntity;
        }
        return new Entity(FindEntityByComponent(typeId));
    }

    internal static Entity[] FindManyByComponent(int typeId) {
        if (FindEntitiesByComponent == null || typeId < 0) {
            return System.Array.Empty<Entity>();
        }
        int count = FindEntitiesByComponent(typeId, null, 0);
        if (count <= 0) {
            return System.Array.Empty<Entity>();
        }
        var buffer = new NativeEntity[count];
        fixed (NativeEntity* bp = buffer) {
            int written = FindEntitiesByComponent(typeId, bp, count);
            return MakeEntityArray(buffer, written < count ? written : count);
        }
    }

    private static Entity[] MakeEntityArray(NativeEntity[] buffer, int count) {
        var result = new Entity[count];
        for (int i = 0; i < count; ++i) {
            result[i] = new Entity(buffer[i]);
        }
        return result;
    }

    // user settings root の絶対パス。length-query。
    internal static string ReadUserSettingsRoot() {
        if (CopyUserSettingsRoot == null) {
            return string.Empty;
        }
        int needed = CopyUserSettingsRoot(null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyUserSettingsRoot(ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }
}
