using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace NEMEngine;

internal static unsafe class NativeAPI {


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
    internal static delegate* unmanaged[Cdecl]<ulong> ReloadActiveScene;
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
    // v11: EntityRef(sourceAsset, localFileID) を runtime entity へ解決する
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

    // 入力デバイス
    internal static delegate* unmanaged[Cdecl]<int> GetInputType;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, byte*, NativeMaterialParameterValue*, int> SetRendererMaterialParameter;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, NativeMaterialParameterValue*, int> GetRendererMaterialParameter;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, ulong, int> ClearRendererMaterialParameter;
    internal static delegate* unmanaged[Cdecl]<int> IsRayTracingSupported;
    internal static delegate* unmanaged[Cdecl]<int> IsRayTracingActive;
    // v48: RenderFeaturePassのProfile世代付きUUIDハンドル
    internal static delegate* unmanaged[Cdecl]<byte*, ulong*, ulong*, int> ResolveRenderFeaturePass;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int> ValidateRenderFeaturePass;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int, int> SetRenderFeaturePassEnabled;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int, int> SetRenderFeaturePassSceneColorOutput;
    internal static delegate* unmanaged[Cdecl]<byte*, int, int> SetRenderFeatureGroupEnabled;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, ulong, byte*,
        NativeMaterialParameterValue*, int> SetRenderFeaturePassParameter;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, ulong,
        NativeMaterialParameterValue*, int> GetRenderFeaturePassParameter;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, ulong, int> ClearRenderFeaturePassParameter;
    internal static delegate* unmanaged[Cdecl]<ulong, ulong, int> ResetRenderFeaturePass;
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
    // Collision単一形状操作
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void*, int, int> CollisionGetShapeProperty;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void*, int, int> CollisionSetShapeProperty;
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
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int*, int*, int> CanvasGetNavigationTableSize;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, int> CanvasResizeNavigationTable;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, NativeEntity*, int> CanvasGetNavigationCell;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, int, NativeEntity, int> CanvasSetNavigationCell;
    // v29: Application終了要求
    internal static delegate* unmanaged[Cdecl]<void> RequestApplicationQuit;
    // v30: GameViewとCanvas座標変換
    internal static delegate* unmanaged[Cdecl]<NativeVector3, NativeVector3*, int> WorldToScreenPoint;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector2, NativeVector2*, int> CanvasScreenToLocalPoint;

    internal static delegate* unmanaged[Cdecl]<NativeEntity, ulong, byte*, ulong> BeginScriptSample;
    internal static delegate* unmanaged[Cdecl]<ulong, void> EndScriptSample;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> DontDestroyOnLoad;

    internal static void SetCallbacks(NativeAPITable* callbacks) {
        BeginScriptSample = callbacks->beginScriptSample;
        EndScriptSample = callbacks->endScriptSample;
        DontDestroyOnLoad = callbacks->dontDestroyOnLoad;

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
        ReloadActiveScene = callbacks->reloadActiveScene;
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
        GetMouseRangeControl = callbacks->getMouseRangeControl;
        SetMouseRangeControl = callbacks->setMouseRangeControl;
        SetRendererMaterialParameter = callbacks->setRendererMaterialParameter;
        GetRendererMaterialParameter = callbacks->getRendererMaterialParameter;
        ClearRendererMaterialParameter = callbacks->clearRendererMaterialParameter;
        IsRayTracingSupported = callbacks->isRayTracingSupported;
        IsRayTracingActive = callbacks->isRayTracingActive;
        ResolveRenderFeaturePass = callbacks->resolveRenderFeaturePass;
        ValidateRenderFeaturePass = callbacks->validateRenderFeaturePass;
        SetRenderFeaturePassEnabled = callbacks->setRenderFeaturePassEnabled;
        SetRenderFeaturePassSceneColorOutput = callbacks->setRenderFeaturePassSceneColorOutput;
        SetRenderFeatureGroupEnabled = callbacks->setRenderFeatureGroupEnabled;
        SetRenderFeaturePassParameter = callbacks->setRenderFeaturePassParameter;
        GetRenderFeaturePassParameter = callbacks->getRenderFeaturePassParameter;
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
        CanvasGetNavigationTableSize = callbacks->canvasGetNavigationTableSize;
        CanvasResizeNavigationTable = callbacks->canvasResizeNavigationTable;
        CanvasGetNavigationCell = callbacks->canvasGetNavigationCell;
        CanvasSetNavigationCell = callbacks->canvasSetNavigationCell;
        RequestApplicationQuit = callbacks->requestApplicationQuit;
        WorldToScreenPoint = callbacks->worldToScreenPoint;
        CanvasScreenToLocalPoint = callbacks->canvasScreenToLocalPoint;
        AudioPlayOneShot = callbacks->audioPlayOneShot;
        AudioUnPause = callbacks->audioUnPause;
    }

	internal static bool ValidateRenderFeaturePassValue(
		ulong passID, ulong generation) =>
		ValidateRenderFeaturePass != null && passID != 0ul &&
		generation != 0ul &&
		ValidateRenderFeaturePass(passID, generation) != 0;

	internal static bool WriteRenderFeaturePassEnabled(
		ulong passID, ulong generation, bool enabled) =>
		SetRenderFeaturePassEnabled != null && passID != 0ul &&
		generation != 0ul && SetRenderFeaturePassEnabled(
			passID, generation, enabled ? 1 : 0) != 0;

	internal static bool WriteRenderFeaturePassSceneColorOutput(
		ulong passID, ulong generation, bool enabled) =>
		SetRenderFeaturePassSceneColorOutput != null &&
		SetRenderFeaturePassSceneColorOutput(passID, generation, enabled ? 1 : 0) != 0;

	internal static bool WriteRenderFeaturePassParameter(
		ulong passID, ulong generation, ulong parameterID,
		string parameterName,
		NativeMaterialParameterValue value) {

		if (SetRenderFeaturePassParameter == null || parameterID == 0ul ||
			passID == 0ul || generation == 0ul ||
			string.IsNullOrEmpty(parameterName)) {

			return false;
		}
        int parameterByteCount = Encoding.UTF8.GetByteCount(parameterName);
        Span<byte> parameterBytes = parameterByteCount < 256
            ? stackalloc byte[parameterByteCount + 1]
            : new byte[parameterByteCount + 1];
        Encoding.UTF8.GetBytes(parameterName, parameterBytes);
        parameterBytes[parameterByteCount] = 0;
		fixed (byte* parameterNamePtr = parameterBytes) {
			return SetRenderFeaturePassParameter(
				passID, generation, parameterID,
				parameterNamePtr, &value) != 0;
		}
	}

	internal static bool ReadRenderFeaturePassParameter(
		ulong passID, ulong generation, ulong parameterID,
		out NativeMaterialParameterValue value) {

		NativeMaterialParameterValue result = default;
		bool found = GetRenderFeaturePassParameter != null &&
			passID != 0ul && generation != 0ul && parameterID != 0ul &&
			GetRenderFeaturePassParameter(
				passID, generation, parameterID, &result) != 0;
		value = result;
		return found;
	}

	internal static bool WriteRenderFeatureGroupEnabled(
		string groupName, bool enabled) {

		if (SetRenderFeatureGroupEnabled == null ||
			string.IsNullOrEmpty(groupName)) {
			return false;
		}
		int byteCount = Encoding.UTF8.GetByteCount(groupName);
		Span<byte> bytes = byteCount < 256
			? stackalloc byte[byteCount + 1]
			: new byte[byteCount + 1];
		Encoding.UTF8.GetBytes(groupName, bytes);
		bytes[byteCount] = 0;
		fixed (byte* groupNamePtr = bytes) {
			return SetRenderFeatureGroupEnabled(
				groupNamePtr, enabled ? 1 : 0) != 0;
		}
	}

	internal static bool ClearRenderFeaturePassParameterValue(
		ulong passID, ulong generation, ulong parameterID) =>
		ClearRenderFeaturePassParameter != null && passID != 0ul &&
		generation != 0ul && parameterID != 0ul &&
		ClearRenderFeaturePassParameter(
			passID, generation, parameterID) != 0;

	internal static bool ResetRenderFeaturePassValue(
		ulong passID, ulong generation) =>
		ResetRenderFeaturePass != null && passID != 0ul &&
		generation != 0ul &&
		ResetRenderFeaturePass(passID, generation) != 0;

	internal static void ResetAllRenderFeatureOverrides() {

		if (ResetRenderFeatureOverrides != null) {
			ResetRenderFeatureOverrides();
		}
	}

    // POD property を outValue へ取得する。失敗時は outValue を変更しない
    internal static void ComponentGet(NativeEntity entity, int typeID, int propertyID, void* outValue, int valueSize) {
        if (GetComponentProperty != null) {
            GetComponentProperty(entity, typeID, propertyID, outValue, valueSize);
        }
    }

    internal static void ComponentSet(NativeEntity entity, int typeID, int propertyID, void* value, int valueSize) {
        if (SetComponentProperty != null) {
            SetComponentProperty(entity, typeID, propertyID, value, valueSize);
        }
    }

    // string property を length query + buffer で取得する（固定長 buffer を使わない）
    internal static string ComponentGetString(NativeEntity entity, int typeID, int propertyID) {
        if (GetComponentStringProperty == null) {
            return string.Empty;
        }
        // まず必要 byte 数を問い合わせる（buffer=null, capacity=0 → written に必要量）
        int needed = 0;
        GetComponentStringProperty(entity, typeID, propertyID, null, 0, &needed);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed];
        int written = 0;
        fixed (byte* ptr = bytes) {
            GetComponentStringProperty(entity, typeID, propertyID, ptr, needed, &written);
        }
        return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
    }

    internal static void ComponentSetString(NativeEntity entity, int typeID, int propertyID, string value) {
        if (SetComponentStringProperty == null) {
            return;
        }
        string safe = value ?? string.Empty;
        byte[] bytes = Encoding.UTF8.GetBytes(safe);
        fixed (byte* ptr = bytes) {
            SetComponentStringProperty(entity, typeID, propertyID, ptr, bytes.Length);
        }
    }

	internal static bool ReadUIBlocksGameplayInput() {
		return GetUIBlocksGameplayInput != null && GetUIBlocksGameplayInput() != 0;
	}

}
