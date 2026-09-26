#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>

Engine::ManagedNativeAPITable Engine::ManagedScriptRuntime::CreateNativeCallbacks() {

	ManagedNativeAPITable callbacks{};
	// ABIヘッダを先頭に設定する、C#側はバージョンとサイズと機能を検証し不一致なら初期化を拒否する
	callbacks.beginScriptSample = [](ManagedNativeEntity entity, uint64_t slotID, const char* name) -> uint64_t {
		return ScriptProfiler::GetInstance().BeginDetail(entity, slotID, name);
	};
	callbacks.endScriptSample = [](uint64_t token) {
		ScriptProfiler::GetInstance().End(token, true);
	};
	callbacks.header.abiVersion = kManagedAbiVersion;
	callbacks.header.structSize = static_cast<uint32_t>(sizeof(ManagedNativeAPITable));
	callbacks.header.capabilities = kManagedCapabilitiesAll;
	callbacks.header.bindingFingerprint = ManagedNativeAPITable::kBindingFingerprint;
	callbacks.log = &ManagedScriptRuntime::LogCallback;
	callbacks.getDeltaTime = &ManagedScriptRuntime::GetDeltaTimeCallback;
	callbacks.getFixedDeltaTime = &ManagedScriptRuntime::GetFixedDeltaTimeCallback;
	callbacks.getKey = &ManagedScriptRuntime::GetKeyCallback;
	callbacks.getKeyDown = &ManagedScriptRuntime::GetKeyDownCallback;
	callbacks.getKeyUp = &ManagedScriptRuntime::GetKeyUpCallback;
	callbacks.getMouseButton = &ManagedScriptRuntime::GetMouseButtonCallback;
	callbacks.getMouseButtonDown = &ManagedScriptRuntime::GetMouseButtonDownCallback;
	callbacks.getMouseButtonUp = &ManagedScriptRuntime::GetMouseButtonUpCallback;
	callbacks.getMousePosition = &ManagedScriptRuntime::GetMousePositionCallback;
	callbacks.getMouseDelta = &ManagedScriptRuntime::GetMouseDeltaCallback;
	callbacks.getMouseWheel = &ManagedScriptRuntime::GetMouseWheelCallback;
	callbacks.getGamepadButton = &ManagedScriptRuntime::GetGamepadButtonCallback;
	callbacks.getGamepadButtonDown = &ManagedScriptRuntime::GetGamepadButtonDownCallback;
	callbacks.isGamepadConnected = &ManagedScriptRuntime::IsGamepadConnectedCallback;
	callbacks.getLeftStick = &ManagedScriptRuntime::GetLeftStickCallback;
	callbacks.getRightStick = &ManagedScriptRuntime::GetRightStickCallback;
	callbacks.getLeftTrigger = &ManagedScriptRuntime::GetLeftTriggerCallback;
	callbacks.getRightTrigger = &ManagedScriptRuntime::GetRightTriggerCallback;
	callbacks.isAlive = &ManagedScriptRuntime::IsAliveCallback;
	callbacks.copyName = &ManagedScriptRuntime::CopyNameCallback;
	callbacks.setName = &ManagedScriptRuntime::SetNameCallback;
	callbacks.getActiveSelf = &ManagedScriptRuntime::GetActiveSelfCallback;
	callbacks.setActiveSelf = &ManagedScriptRuntime::SetActiveSelfCallback;
	callbacks.getActiveInHierarchy = &ManagedScriptRuntime::GetActiveInHierarchyCallback;
	callbacks.getParent = &ManagedScriptRuntime::GetParentCallback;
	callbacks.getFirstChild = &ManagedScriptRuntime::GetFirstChildCallback;
	callbacks.getNextSibling = &ManagedScriptRuntime::GetNextSiblingCallback;
	callbacks.setParent = &ManagedScriptRuntime::SetParentCallback;
	callbacks.getPosition = &ManagedScriptRuntime::GetPositionCallback;
	callbacks.setPosition = &ManagedScriptRuntime::SetPositionCallback;
	callbacks.getLocalPosition = &ManagedScriptRuntime::GetLocalPositionCallback;
	callbacks.setLocalPosition = &ManagedScriptRuntime::SetLocalPositionCallback;
	callbacks.getLocalScale = &ManagedScriptRuntime::GetLocalScaleCallback;
	callbacks.setLocalScale = &ManagedScriptRuntime::SetLocalScaleCallback;
	callbacks.getLocalRotation = &ManagedScriptRuntime::GetLocalRotationCallback;
	callbacks.setLocalRotation = &ManagedScriptRuntime::SetLocalRotationCallback;
	callbacks.getRotation = &ManagedScriptRuntime::GetRotationCallback;
	callbacks.setRotation = &ManagedScriptRuntime::SetRotationCallback;
	callbacks.getLossyScale = &ManagedScriptRuntime::GetLossyScaleCallback;
	callbacks.hasComponent = &ManagedScriptRuntime::HasComponentCallback;
	callbacks.getComponentInstanceID = &ManagedScriptRuntime::GetComponentInstanceIDCallback;
	callbacks.addComponent = &ManagedScriptRuntime::AddComponentCallback;
	callbacks.removeComponent = &ManagedScriptRuntime::RemoveComponentCallback;
	callbacks.dynamicBufferLength = &ManagedScriptRuntime::DynamicBufferLengthCallback;
	callbacks.dynamicBufferCopy = &ManagedScriptRuntime::DynamicBufferCopyCallback;
	callbacks.dynamicBufferMutate = &ManagedScriptRuntime::DynamicBufferMutateCallback;
	callbacks.destroyEntity = &ManagedScriptRuntime::DestroyEntityCallback;
	callbacks.getScriptEnabled = &ManagedScriptRuntime::GetScriptEnabledCallback;
	callbacks.setScriptEnabled = &ManagedScriptRuntime::SetScriptEnabledCallback;
	callbacks.getScriptInstance = &ManagedScriptRuntime::GetScriptInstanceCallback;
	callbacks.attachScript = &ManagedScriptRuntime::AttachScriptCallback;
	// 自動生成コンポーネントバインディングの型付きプロパティ振り分け、ManagedComponentBindings.json由来
	callbacks.getComponentProperty = &GeneratedComponentBindings::GetComponentProperty;
	callbacks.setComponentProperty = &GeneratedComponentBindings::SetComponentProperty;
	callbacks.getComponentStringProperty = &GeneratedComponentBindings::GetComponentStringProperty;
	callbacks.setComponentStringProperty = &GeneratedComponentBindings::SetComponentStringProperty;
	// Gameplay v7のTime拡張とTimeScale
	callbacks.getUnscaledDeltaTime = &ManagedScriptRuntime::GetUnscaledDeltaTimeCallback;
	callbacks.getUnscaledFixedDeltaTime = &ManagedScriptRuntime::GetUnscaledFixedDeltaTimeCallback;
	callbacks.getTimeSinceStartup = &ManagedScriptRuntime::GetTimeSinceStartupCallback;
	callbacks.getUnscaledTime = &ManagedScriptRuntime::GetUnscaledTimeCallback;
	callbacks.getTimeScale = &ManagedScriptRuntime::GetTimeScaleCallback;
	callbacks.setTimeScale = &ManagedScriptRuntime::SetTimeScaleCallback;
	callbacks.getInputType = &ManagedScriptRuntime::GetInputTypeCallback;
	callbacks.getMouseRangeControl = &ManagedScriptRuntime::GetMouseRangeControlCallback;
	callbacks.setMouseRangeControl = &ManagedScriptRuntime::SetMouseRangeControlCallback;
	callbacks.setRendererMaterialParameter =
		&ManagedScriptRuntime::SetRendererMaterialParameterCallback;
	callbacks.getRendererMaterialParameter =
		&ManagedScriptRuntime::GetRendererMaterialParameterCallback;
	callbacks.clearRendererMaterialParameter =
		&ManagedScriptRuntime::ClearRendererMaterialParameterCallback;
	callbacks.isRayTracingSupported =
		&ManagedScriptRuntime::IsRayTracingSupportedCallback;
	callbacks.isRayTracingActive =
		&ManagedScriptRuntime::IsRayTracingActiveCallback;
	callbacks.resolveRenderFeaturePass =
		&ManagedScriptRuntime::ResolveRenderFeaturePassCallback;
	callbacks.validateRenderFeaturePass =
		&ManagedScriptRuntime::ValidateRenderFeaturePassCallback;
	callbacks.setRenderFeaturePassEnabled =
		&ManagedScriptRuntime::SetRenderFeaturePassEnabledCallback;
	callbacks.setRenderFeaturePassSceneColorOutput =
		&ManagedScriptRuntime::SetRenderFeaturePassSceneColorOutputCallback;
	callbacks.setRenderFeatureGroupEnabled =
		&ManagedScriptRuntime::SetRenderFeatureGroupEnabledCallback;
	callbacks.setRenderFeaturePassParameter =
		&ManagedScriptRuntime::SetRenderFeaturePassParameterCallback;
	callbacks.getRenderFeaturePassParameter =
		&ManagedScriptRuntime::GetRenderFeaturePassParameterCallback;
	callbacks.clearRenderFeaturePassParameter =
		&ManagedScriptRuntime::ClearRenderFeaturePassParameterCallback;
	callbacks.resetRenderFeaturePass =
		&ManagedScriptRuntime::ResetRenderFeaturePassCallback;
	callbacks.resetRenderFeatureOverrides =
		&ManagedScriptRuntime::ResetRenderFeatureOverridesCallback;
	callbacks.collisionGetShapeProperty = &ManagedScriptRuntime::CollisionGetShapePropertyCallback;
	callbacks.collisionSetShapeProperty = &ManagedScriptRuntime::CollisionSetShapePropertyCallback;
	callbacks.getSkinnedAnimationDuration = &ManagedScriptRuntime::GetSkinnedAnimationDurationCallback;
	callbacks.playSkinnedAnimation = &ManagedScriptRuntime::PlaySkinnedAnimationCallback;
	callbacks.copySkinnedAnimationCurrentClip =
		&ManagedScriptRuntime::CopySkinnedAnimationCurrentClipCallback;
	callbacks.getSkinnedAnimationRuntimeState =
		&ManagedScriptRuntime::GetSkinnedAnimationRuntimeStateCallback;
	callbacks.particleSystemControl =
		&ManagedScriptRuntime::ParticleSystemControlCallback;
	callbacks.particleSystemState =
		&ManagedScriptRuntime::ParticleSystemStateCallback;
	callbacks.getUIBlocksGameplayInput = &ManagedScriptRuntime::GetUIBlocksGameplayInputCallback;
	callbacks.getUISelectableRuntimeState =
		&ManagedScriptRuntime::GetUISelectableRuntimeStateCallback;
	callbacks.getUIProgressRuntimeState =
		&ManagedScriptRuntime::GetUIProgressRuntimeStateCallback;
	callbacks.getCanvasInputLocked =
		&ManagedScriptRuntime::GetCanvasInputLockedCallback;
	callbacks.getUIButtonClicked =
		&ManagedScriptRuntime::GetUIButtonClickedCallback;
	callbacks.canvasCopyInputBindings = &ManagedScriptRuntime::CanvasCopyInputBindingsCallback;
	callbacks.canvasSetInputBindings = &ManagedScriptRuntime::CanvasSetInputBindingsCallback;
	callbacks.canvasGetNavigationTableSize =
		&ManagedScriptRuntime::CanvasGetNavigationTableSizeCallback;
	callbacks.canvasResizeNavigationTable =
		&ManagedScriptRuntime::CanvasResizeNavigationTableCallback;
	callbacks.canvasGetNavigationCell =
		&ManagedScriptRuntime::CanvasGetNavigationCellCallback;
	callbacks.canvasSetNavigationCell =
		&ManagedScriptRuntime::CanvasSetNavigationCellCallback;
	callbacks.requestApplicationQuit = &ManagedScriptRuntime::RequestApplicationQuitCallback;
	callbacks.worldToScreenPoint = &ManagedScriptRuntime::WorldToScreenPointCallback;
	callbacks.canvasScreenToLocalPoint = &ManagedScriptRuntime::CanvasScreenToLocalPointCallback;
	callbacks.audioPlayOneShot = &ManagedScriptRuntime::AudioPlayOneShotCallback;
	callbacks.audioUnPause = &ManagedScriptRuntime::AudioUnPauseCallback;
	callbacks.getEntityReferenceIdentity = &ManagedScriptRuntime::GetEntityReferenceIdentityCallback;
	// v21のレイキャストとカメラレイとCollisionタイプ名解決
	callbacks.physicsRaycast = &ManagedScriptRuntime::PhysicsRaycastCallback;
	callbacks.physicsRaycastAll = &ManagedScriptRuntime::PhysicsRaycastAllCallback;
	callbacks.screenPointToRay = &ManagedScriptRuntime::ScreenPointToRayCallback;
	callbacks.getMousePositionInView = &ManagedScriptRuntime::GetMousePositionInViewCallback;
	callbacks.getCollisionTypeMaskByName = &ManagedScriptRuntime::GetCollisionTypeMaskByNameCallback;
	callbacks.easedValue = &ManagedScriptRuntime::EasedValueCallback;
	callbacks.getFrameCount = &ManagedScriptRuntime::GetFrameCountCallback;
	// Gameplay v7のAssetRef実行時解決
	callbacks.assetExists = &ManagedScriptRuntime::AssetExistsCallback;
	callbacks.copyAssetDisplayName = &ManagedScriptRuntime::CopyAssetDisplayNameCallback;
	// Gameplay v7のEntity生成/Prefab/Scene/SetParent
	callbacks.createEntity = &ManagedScriptRuntime::CreateEntityCallback;
	callbacks.instantiatePrefab = &ManagedScriptRuntime::InstantiatePrefabCallback;
	callbacks.loadSceneAdditive = &ManagedScriptRuntime::LoadSceneAdditiveCallback;
	callbacks.loadSceneSingle = &ManagedScriptRuntime::LoadSceneSingleCallback;
	callbacks.reloadActiveScene = &ManagedScriptRuntime::ReloadActiveSceneCallback;
	callbacks.dontDestroyOnLoad = &ManagedScriptRuntime::DontDestroyOnLoadCallback;
	callbacks.resolveEntityRef = &ManagedScriptRuntime::ResolveEntityRefCallback;
	// ライン描画v12のcomponent点列設定と即時描画
	callbacks.lineSetPoints = &ManagedScriptRuntime::LineSetPointsCallback;
	callbacks.lineDrawImmediate = &ManagedScriptRuntime::LineDrawImmediateCallback;
	callbacks.lineDrawSphereImmediate = &ManagedScriptRuntime::LineDrawSphereImmediateCallback;
	callbacks.lineAddPoint = &ManagedScriptRuntime::LineAddPointCallback;
	callbacks.lineUpdatePoint = &ManagedScriptRuntime::LineUpdatePointCallback;
	callbacks.unloadScene = &ManagedScriptRuntime::UnloadSceneCallback;
	callbacks.isSceneInstanceAlive = &ManagedScriptRuntime::IsSceneInstanceAliveCallback;
	callbacks.setParentKeepWorld = &ManagedScriptRuntime::SetParentKeepWorldCallback;
	// Gameplay v7の入力拡張で複数ゲームパッドや軸や文字や入力フォーカス
	callbacks.getGamepadButtonIndexed = &ManagedScriptRuntime::GetGamepadButtonIndexedCallback;
	callbacks.getGamepadButtonDownIndexed = &ManagedScriptRuntime::GetGamepadButtonDownIndexedCallback;
	callbacks.getGamepadButtonUpIndexed = &ManagedScriptRuntime::GetGamepadButtonUpIndexedCallback;
	callbacks.getGamepadAxis = &ManagedScriptRuntime::GetGamepadAxisCallback;
	callbacks.isGamepadConnectedIndexed = &ManagedScriptRuntime::IsGamepadConnectedIndexedCallback;
	callbacks.getConnectedGamepadCount = &ManagedScriptRuntime::GetConnectedGamepadCountCallback;
	callbacks.getHasFocus = &ManagedScriptRuntime::GetHasFocusCallback;
	callbacks.copyTextInput = &ManagedScriptRuntime::CopyTextInputCallback;
	callbacks.copyProjectRoot = &ManagedScriptRuntime::CopyProjectRootCallback;
	callbacks.copyUserSettingsRoot = &ManagedScriptRuntime::CopyUserSettingsRootCallback;
	// Gameplay v7のAudioSourceメソッド
	callbacks.audioPlay = &ManagedScriptRuntime::AudioPlayCallback;
	callbacks.audioPause = &ManagedScriptRuntime::AudioPauseCallback;
	callbacks.audioStop = &ManagedScriptRuntime::AudioStopCallback;
	callbacks.audioIsPlaying = &ManagedScriptRuntime::AudioIsPlayingCallback;
	callbacks.reportScriptException = &ManagedScriptRuntime::ReportScriptExceptionCallback;
	// v14のTag公開とLayerマスク公開とEntity検索
	callbacks.copyTag = &ManagedScriptRuntime::CopyTagCallback;
	callbacks.setTag = &ManagedScriptRuntime::SetTagCallback;
	callbacks.getVisibilityLayerMask = &ManagedScriptRuntime::GetVisibilityLayerMaskCallback;
	callbacks.setVisibilityLayerMask = &ManagedScriptRuntime::SetVisibilityLayerMaskCallback;
	callbacks.getCollisionTypeMask = &ManagedScriptRuntime::GetCollisionTypeMaskCallback;
	callbacks.getCollisionRuntimeState = &ManagedScriptRuntime::GetCollisionRuntimeStateCallback;
	callbacks.setCollisionTypeMask = &ManagedScriptRuntime::SetCollisionTypeMaskCallback;
	callbacks.findEntityByName = &ManagedScriptRuntime::FindEntityByNameCallback;
	callbacks.findEntityByTag = &ManagedScriptRuntime::FindEntityByTagCallback;
	callbacks.findEntitiesByTag = &ManagedScriptRuntime::FindEntitiesByTagCallback;
	callbacks.findEntityByComponent = &ManagedScriptRuntime::FindEntityByComponentCallback;
	callbacks.findEntitiesByComponent = &ManagedScriptRuntime::FindEntitiesByComponentCallback;
	callbacks.lineDrawShape = &ManagedScriptRuntime::LineDrawShapeCallback;
	// v16のTransform親追従の継承フラグ
	callbacks.getIgnoreParentRotation = &ManagedScriptRuntime::GetIgnoreParentRotationCallback;
	callbacks.setIgnoreParentRotation = &ManagedScriptRuntime::SetIgnoreParentRotationCallback;
	callbacks.getIgnoreParentScale = &ManagedScriptRuntime::GetIgnoreParentScaleCallback;
	callbacks.setIgnoreParentScale = &ManagedScriptRuntime::SetIgnoreParentScaleCallback;

	return callbacks;
}
