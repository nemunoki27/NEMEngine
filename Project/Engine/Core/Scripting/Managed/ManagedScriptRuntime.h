#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Scripting/Managed/ManagedBridgeExports.h>
#include <Engine/Core/Scripting/Managed/ManagedSchemaCache.h>
#include <Engine/Core/Scripting/Managed/DotnetHostResolver.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedALCStatus.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <chrono>
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <vector>

// json
#include <json.hpp>

namespace Engine {

	// 前方宣言
	class ECSWorld;
	struct SystemContext;

	//============================================================================
	//	ManagedScriptRuntime
	//	C#スクリプトのロード、型情報取得、ライフサイクル呼び出しを管理する
	//============================================================================
	class ManagedScriptRuntime {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ManagedScriptRuntime() = default;
		~ManagedScriptRuntime() = default;

		// .NETランタイムとスクリプトアセンブリの初期化
		bool Init();
		// 終了処理
		void Finalize();

		// C#スクリプト型をビヘイビアレジストリへ反映する
		void RefreshScriptTypes();
		// ゲーム側Assemblyを最新の出力から読み直す
		bool ReloadGameAssembly(bool waitForManagedDebugger = false);
		// 指定Assemblyへ切り替えて型情報を更新する
		bool LoadGameAssemblyFromPath(const std::filesystem::path& dllPath, bool waitForManagedDebugger = false);
		// ゲーム側C#アセンブリを解放する
		void UnloadGameAssembly();

		// 型GUIDからEntity上のScriptを生成する
		ManagedScriptInstanceHandle CreateInstance(const std::string& scriptTypeID, ECSWorld& world, const Entity& entity,
			const nlohmann::json& serializedFields, uint64_t scriptSlotID);
		// 指定AssemblyからScript一覧を生成する
		ManagedStatus GenerateScriptManifest(const std::filesystem::path& assemblyPath,
			const std::filesystem::path& manifestOutputPath);
		// Scriptへ保存Fieldを設定する
		void SetSerializedFields(ManagedScriptInstanceHandle handle, const nlohmann::json& serializedFields);
		// 生成済みScriptの保留参照をLifecycleより前に解決する
		void FlushPendingReferences(ECSWorld& world);
		// Scriptの実体を破棄する
		void DestroyInstance(ManagedScriptInstanceHandle handle);
		// 詳細計測の対象変更をManaged側へ通知する
		void ConfigureProfiler(const char* typeName, ManagedNativeEntity entity, uint64_t slotID);

		// Lifecycleを呼び出して実行結果を返す
		ManagedStatus InvokeAwake(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeStart(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeOnEnable(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeOnDisable(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeOnDestroy(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeFixedUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeLateUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context);

		// C#側のOnCollisionEnterを呼び出す
		ManagedStatus InvokeCollisionEnter(ManagedScriptInstanceHandle handle, const SystemContext& context,
			const ManagedCollisionEvent& collision);
		// C#側のOnCollisionStayを呼び出す
		ManagedStatus InvokeCollisionStay(ManagedScriptInstanceHandle handle, const SystemContext& context,
			const ManagedCollisionEvent& collision);
		// C#側のOnCollisionExitを呼び出す
		ManagedStatus InvokeCollisionExit(ManagedScriptInstanceHandle handle, const SystemContext& context,
			const ManagedCollisionEvent& collision);
		// C#側のOnAnimationEventを呼び出す
		ManagedStatus InvokeAnimationEvent(ManagedScriptInstanceHandle handle, const SystemContext& context, const char* name,
			float floatParam, int32_t intParam, const char* stringParam);

		// 複製用の保存値を取得し、失敗時は出力を維持する
		bool CaptureSavedValueMap(ManagedScriptInstanceHandle handle, ECSWorld& world, nlohmann::json& fields);
		// Hot Reload用にprivate Fieldを含む実行値を取得する
		bool CaptureReloadValueMap(ManagedScriptInstanceHandle handle, ECSWorld& world, nlohmann::json& fields);
		// Hot Reload用の実行値を新しいインスタンスへ復元する
		bool ApplyReloadValueMap(ManagedScriptInstanceHandle handle, ECSWorld& world, const nlohmann::json& fields);
		// 保存FieldからID別の値を作る
		nlohmann::json BuildSerializedValueMap(const nlohmann::json& serializedFields);

		//--------- 時間とEvent --------------------------------------------------

		// SceneとApplicationのEventを通知する
		void PumpSceneEvents(const SystemContext& context);
		// 終了前にApplicationの終了Eventを通知する
		void RaiseApplicationQuitting();
		// C#から受けたApplication終了要求を取得する
		bool ConsumeApplicationQuitRequest();
		// 指定phaseのTimerとCoroutineを進める
		void TickFrame(int32_t phase, const SystemContext& context);

		// Play開始時にWorldの時間状態を初期化する
		static void BeginPlayTime(const ECSWorld* playWorld);
		// 実行中だけ時刻を進めて差分時刻を返す
		static float AdvanceTime(float rawDeltaTime, float fixedDeltaTime, bool advancing);

		//--------- accessor -----------------------------------------------------

		// ゲーム側の構築Projectを解決する
		std::filesystem::path GameScriptProjectPath() const;
		// 読込中のAssemblyのパスを返す
		const std::filesystem::path& ActiveAssemblyPath() const { return gameAssemblyPath_; }
		// ゲーム側Assemblyの読込状態を返す
		bool HasLoadedGameAssembly() const { return gameAssemblyLoaded_; }
		// 登録済みScript型数を返す
		int32_t ManagedScriptTypeCount() const { return lastManagedTypeCount_; }

		// Runtimeの初期化状態を返す
		bool IsInitialized() const { return initialized_; }
		// Inspector用のField情報を取得する
		const ManagedScriptSchema& GetScriptSchema(const std::string& scriptTypeID);

		// 実行中のField値をID別に取得する
		nlohmann::json GetRuntimeSerializedState(ManagedScriptInstanceHandle handle);
		// 実行中のField値だけを更新する
		void SetRuntimeSerializedField(ManagedScriptInstanceHandle handle, ECSWorld& world, const std::string& fieldID,
			const nlohmann::json& value);

		// 実行中の呼出Contextを返す
		static const SystemContext* GetCurrentContext();

		// 直近のAssembly解放結果を返す
		ALCUnloadStatus GetLastALCUnloadStatus();

		// 共有Runtimeを取得する
		static ManagedScriptRuntime& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// Managed側のLifecycle呼出型
		using InvokeFn = ManagedBridgeExports::InvokeFn;
		using InvokeCollisionFn = ManagedBridgeExports::InvokeCollisionFn;

		//============================================================================
		//	ScopedInvocationContext
		//	呼出中のContextを保持して終了時に復元する
		//============================================================================
		class ScopedInvocationContext {
		public:
			//====================================================================
			//	public Methods
			//====================================================================

			explicit ScopedInvocationContext(const SystemContext& context);
			~ScopedInvocationContext();

			ScopedInvocationContext(const ScopedInvocationContext&) = delete;
			ScopedInvocationContext& operator=(const ScopedInvocationContext&) = delete;
		private:
			//====================================================================
			//	private Methods
			//====================================================================

			//--------- variables ------------------------------------------------

			// 呼出前のContext
			const SystemContext* previous_;
		};

		//============================================================================
		//	ScopedReferenceWorld
		//	参照解決中のWorldを保持して終了時に復元する
		//============================================================================
		class ScopedReferenceWorld {
		public:
			//====================================================================
			//	public Methods
			//====================================================================

			explicit ScopedReferenceWorld(ECSWorld& world);
			~ScopedReferenceWorld();

			ScopedReferenceWorld(const ScopedReferenceWorld&) = delete;
			ScopedReferenceWorld& operator=(const ScopedReferenceWorld&) = delete;
		private:
			//====================================================================
			//	private Methods
			//====================================================================

			//--------- variables ------------------------------------------------

			// 参照解決前のWorld
			ECSWorld* previous_;
		};

		//--------- variables ----------------------------------------------------

		// Runtimeの初期化状態
		bool initialized_ = false;
		// ゲームAssemblyの読込状態
		bool gameAssemblyLoaded_ = false;

		// .NETホストの所有
		DotnetHostResolver dotnetHost_;

		// 接続済みのManaged呼出窓口
		ManagedBridgeExports bridge_;

		// 呼出中だけ有効なContext
		static thread_local const SystemContext* currentContext_;
		// 実行時Inspectorの参照デシリアライズ中だけ有効なWorld
		static thread_local ECSWorld* currentReferenceWorld_;

		// 実行時刻と時間倍率
		static float timeScale_;
		static float scaledDeltaTime_;
		static float unscaledDeltaTime_;
		static float fixedDeltaTime_;
		static double timeSinceStartup_;
		static double unscaledTime_;
		static uint64_t frameCount_;

		// コアAssemblyの読込元
		std::filesystem::path scriptCoreAssemblyPath_;
		// 現在のゲームAssemblyの読込元
		std::filesystem::path gameAssemblyPath_;
		// 型GUID別のField情報
		ManagedSchemaCache schemaCache_;
		// 登録済みScript型数
		int32_t lastManagedTypeCount_ = 0;
		// C#のApplication.Quitから受けた遅延終了要求
		bool applicationQuitRequested_ = false;

		//--------- functions ----------------------------------------------------

		// Nativeのcallback表を構築する
		static ManagedNativeAPITable CreateNativeCallbacks();
		// .NETホストを初期化する
		bool LoadHostfxr();
		// 必須Managed関数を取得する
		bool LoadBridgeFunctions();
		// 起動時のゲームAssemblyを読み込む
		bool LoadGameAssembly();
		// .NETホストを解放する
		void ReleaseHostfxr();

		// 呼出contextを保持してScriptを実行する
		ManagedStatus Invoke(InvokeFn function, ManagedScriptInstanceHandle handle, const SystemContext& context);
		// C#側のCollisionイベント関数を呼び出す
		ManagedStatus InvokeCollision(InvokeCollisionFn function, ManagedScriptInstanceHandle handle,
			const SystemContext& context, const ManagedCollisionEvent& collision);

		// 倍率を適用した差分時刻を返す
		static float __cdecl GetDeltaTimeCallback();
		// 固定更新の差分時刻を返す
		static float __cdecl GetFixedDeltaTimeCallback();
		// C#のログをNativeへ渡す
		static void __cdecl LogCallback(int32_t level, const char* message);
		// キーの押下状態を返す
		static int32_t __cdecl GetKeyCallback(int32_t key);
		// キーの押下開始を返す
		static int32_t __cdecl GetKeyDownCallback(int32_t key);
		// キーの解放を返す
		static int32_t __cdecl GetKeyUpCallback(int32_t key);
		// マウスボタンの押下状態を返す
		static int32_t __cdecl GetMouseButtonCallback(int32_t button);
		// マウスボタンの押下開始を返す
		static int32_t __cdecl GetMouseButtonDownCallback(int32_t button);
		// マウスボタンの解放を返す
		static int32_t __cdecl GetMouseButtonUpCallback(int32_t button);
		// マウスの位置を返す
		static ManagedVector2 __cdecl GetMousePositionCallback();
		// マウスの移動量を返す
		static ManagedVector2 __cdecl GetMouseDeltaCallback();
		// マウスのホイール量を返す
		static float __cdecl GetMouseWheelCallback();
		// ゲームパッドのボタン状態を返す
		static int32_t __cdecl GetGamepadButtonCallback(int32_t button);
		// ゲームパッドの押下開始を返す
		static int32_t __cdecl GetGamepadButtonDownCallback(int32_t button);
		// ゲームパッドの接続状態を返す
		static int32_t __cdecl IsGamepadConnectedCallback();
		// 左スティックの入力を返す
		static ManagedVector2 __cdecl GetLeftStickCallback();
		// 右スティックの入力を返す
		static ManagedVector2 __cdecl GetRightStickCallback();
		// 左トリガーの入力を返す
		static float __cdecl GetLeftTriggerCallback();
		// 右トリガーの入力を返す
		static float __cdecl GetRightTriggerCallback();
		// Playerのゲーム入力遮断状態を返す
		static int32_t __cdecl GetUIBlocksGameplayInputCallback(int32_t playerIndex);
		// Entityの生存状態を返す
		static int32_t __cdecl IsAliveCallback(ManagedNativeEntity entity);
		// Entity名を指定バッファへコピーする
		static int32_t __cdecl CopyNameCallback(ManagedNativeEntity entity, char* buffer, int32_t capacity);
		// Entity名を設定する
		static void __cdecl SetNameCallback(ManagedNativeEntity entity, const char* name);
		// Entity自身の有効状態を返す
		static int32_t __cdecl GetActiveSelfCallback(ManagedNativeEntity entity);
		// Entity自身の有効状態を設定する
		static void __cdecl SetActiveSelfCallback(ManagedNativeEntity entity, int32_t active);
		// 親階層を含む有効状態を返す
		static int32_t __cdecl GetActiveInHierarchyCallback(ManagedNativeEntity entity);
		// 親Entityを返す
		static ManagedNativeEntity __cdecl GetParentCallback(ManagedNativeEntity entity);
		// 最初の子Entityを返す
		static ManagedNativeEntity __cdecl GetFirstChildCallback(ManagedNativeEntity entity);
		// 次の兄弟Entityを返す
		static ManagedNativeEntity __cdecl GetNextSiblingCallback(ManagedNativeEntity entity);
		// 親変更をCommandへ登録する
		static void __cdecl SetParentCallback(ManagedNativeEntity entity, ManagedNativeEntity parent);
		// 親変更を検証してCommandへ登録する
		static void EnqueueSetParentCommand(ManagedNativeEntity child, ManagedNativeEntity parent, bool worldPositionStays);
		// ワールド位置を返す
		static ManagedVector3 __cdecl GetPositionCallback(ManagedNativeEntity entity);
		// ワールド位置を設定する
		static void __cdecl SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value);
		// ローカル位置を返す
		static ManagedVector3 __cdecl GetLocalPositionCallback(ManagedNativeEntity entity);
		// ローカル位置を設定する
		static void __cdecl SetLocalPositionCallback(ManagedNativeEntity entity, ManagedVector3 value);
		// ローカル拡縮率を返す
		static ManagedVector3 __cdecl GetLocalScaleCallback(ManagedNativeEntity entity);
		// ローカル拡縮率を設定する
		static void __cdecl SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value);
		// ローカル回転を返す
		static ManagedQuaternion __cdecl GetLocalRotationCallback(ManagedNativeEntity entity);
		// ローカル回転を設定する
		static void __cdecl SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value);
		// ワールド回転を返す
		static ManagedQuaternion __cdecl GetRotationCallback(ManagedNativeEntity entity);
		// ワールド回転を設定する
		static void __cdecl SetRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value);
		// ワールド拡縮率を返す
		static ManagedVector3 __cdecl GetLossyScaleCallback(ManagedNativeEntity entity);
		// 親回転の継承除外状態を返す
		static int32_t __cdecl GetIgnoreParentRotationCallback(ManagedNativeEntity entity);
		// 親回転の継承除外を設定する
		static void __cdecl SetIgnoreParentRotationCallback(ManagedNativeEntity entity, int32_t value);
		// 親拡縮率の継承除外状態を返す
		static int32_t __cdecl GetIgnoreParentScaleCallback(ManagedNativeEntity entity);
		// 親拡縮率の継承除外を設定する
		static void __cdecl SetIgnoreParentScaleCallback(ManagedNativeEntity entity, int32_t value);
		// Componentの有無を返す
		static int32_t __cdecl HasComponentCallback(ManagedNativeEntity entity, int32_t typeID);
		// Componentの実行IDを返す
		static uint64_t __cdecl GetComponentInstanceIDCallback(ManagedNativeEntity entity, int32_t typeID);
		// Component追加を予約する
		static void __cdecl AddComponentCallback(ManagedNativeEntity entity, int32_t typeID);
		// Component削除を予約する
		static void __cdecl RemoveComponentCallback(ManagedNativeEntity entity, int32_t typeID);
		// Bufferの要素数を返す
		static int32_t __cdecl DynamicBufferLengthCallback(ManagedNativeEntity entity, int32_t typeID, int32_t elementSize);
		// Bufferの要素を指定範囲へコピーする
		static int32_t __cdecl DynamicBufferCopyCallback(ManagedNativeEntity entity, int32_t typeID, int32_t elementSize,
			int32_t startIndex, void* destination, int32_t capacity);
		// Bufferへ要素の変更を適用する
		static int32_t __cdecl DynamicBufferMutateCallback(ManagedNativeEntity entity, int32_t typeID, int32_t elementSize,
			int32_t operation, int32_t index, const void* data, int32_t count);
		// Entityの破棄を予約する
		static void __cdecl DestroyEntityCallback(ManagedNativeEntity entity);
		// Scriptの有効状態を返す
		static int32_t __cdecl GetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID);
		// Scriptの有効状態を設定する
		static void __cdecl SetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID, int32_t enabled);
		// Entity上のScriptを型IDで検索する
		static ManagedScriptInstanceHandle __cdecl GetScriptInstanceCallback(ManagedNativeEntity owner,
			const char* scriptTypeID);
		// EntityへScriptを追加する
		static ManagedScriptInstanceHandle __cdecl AttachScriptCallback(ManagedNativeEntity owner, const char* scriptTypeID);
		// EntityからScriptを削除する
		static void __cdecl RemoveScriptCallback(ManagedNativeEntity owner, uint64_t scriptSlotID);
		// 倍率適用前の差分時刻を返す
		static float __cdecl GetUnscaledDeltaTimeCallback();
		// 倍率適用前の固定差分時刻を返す
		static float __cdecl GetUnscaledFixedDeltaTimeCallback();
		// 倍率適用後の経過時間を返す
		static double __cdecl GetTimeSinceStartupCallback();
		// 倍率適用前の経過時間を返す
		static double __cdecl GetUnscaledTimeCallback();
		// 時間倍率を返す
		static float __cdecl GetTimeScaleCallback();
		// 時間倍率を設定する
		static void __cdecl SetTimeScaleCallback(float value);
		// 実行中のフレーム数を返す
		static uint64_t __cdecl GetFrameCountCallback();
		// フレーム終端の終了を要求する
		static void __cdecl RequestApplicationQuitCallback();

		// 現在の入力種別を返す
		static int32_t __cdecl GetInputTypeCallback();
		// マウス範囲制御の状態を返す
		static int32_t __cdecl GetMouseRangeControlCallback();
		// マウス範囲制御を設定する
		static void __cdecl SetMouseRangeControlCallback(int32_t enabled);
		// RendererのMaterial値を設定する
		static int32_t __cdecl SetRendererMaterialParameterCallback(ManagedNativeEntity entity, int32_t target,
			int32_t subMeshIndex, uint64_t parameterID, const char* name, const ManagedMaterialParameterValue* value);
		// RendererのMaterial値を取得する
		static int32_t __cdecl GetRendererMaterialParameterCallback(ManagedNativeEntity entity, int32_t target,
			int32_t subMeshIndex, uint64_t parameterID, ManagedMaterialParameterValue* outValue);
		// RendererのMaterial値を解除する
		static int32_t __cdecl ClearRendererMaterialParameterCallback(ManagedNativeEntity entity, int32_t target,
			int32_t subMeshIndex, uint64_t parameterID);
		// GPUのRayTracing対応を返す
		static int32_t __cdecl IsRayTracingSupportedCallback();
		// RayTracingの実行状態を返す
		static int32_t __cdecl IsRayTracingActiveCallback();
		// 名前から描画Passと世代を解決する
		static int32_t __cdecl ResolveRenderFeaturePassCallback(const char* passName, uint64_t* outPassID,
			uint64_t* outGeneration);
		// 描画Passの世代を検証する
		static int32_t __cdecl ValidateRenderFeaturePassCallback(uint64_t passID, uint64_t generation);
		// 実行中の描画Passを切り替える
		static int32_t __cdecl SetRenderFeaturePassEnabledCallback(uint64_t passID, uint64_t generation, int32_t enabled);
		// 描画Passの画面出力を切り替える
		static int32_t __cdecl SetRenderFeaturePassSceneColorOutputCallback(uint64_t passID, uint64_t generation,
			int32_t enabled);
		// 実行中の描画Groupを切り替える
		static int32_t __cdecl SetRenderFeatureGroupEnabledCallback(const char* groupName, int32_t enabled);
		// 実行中の描画Pass値を設定する
		static int32_t __cdecl SetRenderFeaturePassParameterCallback(uint64_t passID, uint64_t generation,
			uint64_t parameterID, const char* parameterName, const ManagedMaterialParameterValue* value);
		// 実行中の描画Pass値を取得する
		static int32_t __cdecl GetRenderFeaturePassParameterCallback(uint64_t passID, uint64_t generation,
			uint64_t parameterID, ManagedMaterialParameterValue* outValue);
		// 実行中の描画Pass値を解除する
		static int32_t __cdecl ClearRenderFeaturePassParameterCallback(uint64_t passID, uint64_t generation,
			uint64_t parameterID);
		// 描画Passの実行変更を解除する
		static int32_t __cdecl ResetRenderFeaturePassCallback(uint64_t passID, uint64_t generation);
		// 描画構成の実行変更を解除する
		static void __cdecl ResetRenderFeatureOverridesCallback();
		// Colliderの形状値を取得する
		static int32_t __cdecl CollisionGetShapePropertyCallback(ManagedNativeEntity entity, int32_t propertyID, void* out,
			int32_t size);
		// Colliderの形状値を設定する
		static int32_t __cdecl CollisionSetShapePropertyCallback(ManagedNativeEntity entity, int32_t propertyID,
			const void* value, int32_t size);
		// 指定Clipの再生時間を返す
		static float __cdecl GetSkinnedAnimationDurationCallback(ManagedNativeEntity entity, const char* clipName);
		// 指定Clipを先頭から再生する
		static void __cdecl PlaySkinnedAnimationCallback(ManagedNativeEntity entity, const char* clipName);
		// 再生中のClip名をコピーする
		static int32_t __cdecl CopySkinnedAnimationCurrentClipCallback(ManagedNativeEntity entity, char* buffer,
			int32_t capacity);
		// 骨格アニメーションの実行状態を返す
		static int32_t __cdecl GetSkinnedAnimationRuntimeStateCallback(ManagedNativeEntity entity,
			ManagedSkinnedAnimationRuntimeState* outState);
		// UIの選択状態を返す
		static int32_t __cdecl GetUISelectableRuntimeStateCallback(ManagedNativeEntity entity,
			ManagedUISelectableRuntimeState* outState);
		// UIの補間済み表示値を返す
		static int32_t __cdecl GetUIProgressRuntimeStateCallback(ManagedNativeEntity entity,
			ManagedUIProgressRuntimeState* outState);
		// Canvasの入力遮断状態を返す
		static int32_t __cdecl GetCanvasInputLockedCallback(ManagedNativeEntity entity);
		// UIボタンのクリック状態を返す
		static int32_t __cdecl GetUIButtonClickedCallback(ManagedNativeEntity entity, int32_t buttonType);
		// Assetの存在を確認する
		static int32_t __cdecl AssetExistsCallback(ManagedAssetGUID assetID);
		// Assetの表示名をコピーする
		static int32_t __cdecl CopyAssetDisplayNameCallback(ManagedAssetGUID assetID, char* buffer, int32_t capacity);
		// Entityの生成を予約する
		static ManagedNativeEntity __cdecl CreateEntityCallback(const char* name, ManagedNativeEntity parent);
		// Prefabの実体生成を予約する
		static ManagedNativeEntity __cdecl InstantiatePrefabCallback(ManagedAssetGUID prefabAssetID, ManagedVector3 position,
			ManagedQuaternion rotation, int32_t useTransform, ManagedNativeEntity parent);
		// Entity階層の複製を予約する
		static ManagedNativeEntity __cdecl InstantiateEntityCallback(ManagedNativeEntity source, ManagedVector3 position,
			ManagedQuaternion rotation, int32_t useTransform, ManagedNativeEntity parent);
		// Sceneの追加読込を要求する
		static uint64_t __cdecl LoadSceneAdditiveCallback(ManagedAssetGUID sceneAssetID);
		// Sceneの事前読込を要求する
		static uint64_t __cdecl PreloadSceneCallback(ManagedAssetGUID sceneAssetID);
		// Sceneの単独読込を要求する
		static uint64_t __cdecl LoadSceneSingleCallback(ManagedAssetGUID sceneAssetID);
		// ActiveSceneの再読込を要求する
		static uint64_t __cdecl ReloadActiveSceneCallback();
		// Scene切替時にEntityを保持する
		static int32_t __cdecl DontDestroyOnLoadCallback(ManagedNativeEntity entity);
		// 保存参照を対象Entityへ解決する
		static ManagedNativeEntity __cdecl ResolveEntityRefCallback(ManagedAssetGUID sourceAsset, uint64_t localFileID,
			ManagedNativeEntity owner);
		// Entityの保存参照IDを取得する
		static void __cdecl GetEntityReferenceIdentityCallback(ManagedNativeEntity entity, ManagedAssetGUID* sourceAsset,
			uint64_t* localFileID, int32_t* kind);
		// レイに最も近い接触を返す
		static int32_t __cdecl PhysicsRaycastCallback(ManagedVector3 origin, ManagedVector3 direction, float maxDistance,
			uint32_t layerMask, uint32_t targets, uint32_t triggerInteraction, ManagedRaycastHit* outHit);
		// レイの全接触を距離順で返す
		static int32_t __cdecl PhysicsRaycastAllCallback(ManagedVector3 origin, ManagedVector3 direction, float maxDistance,
			uint32_t layerMask, uint32_t targets, uint32_t triggerInteraction, ManagedRaycastHit* buffer, int32_t capacity);
		// QueryのTrigger対象設定を返す
		static int32_t __cdecl GetQueriesHitTriggersCallback();
		// QueryのTrigger対象設定を変更する
		static void __cdecl SetQueriesHitTriggersCallback(int32_t enabled);
		// 画面座標からワールドレイを作る
		static int32_t __cdecl ScreenPointToRayCallback(float x, float y, ManagedVector3* outOrigin,
			ManagedVector3* outDirection);
		// ワールド座標を画面座標へ変換する
		static int32_t __cdecl WorldToScreenPointCallback(ManagedVector3 worldPosition, ManagedVector3* outScreenPosition);
		// View内のマウス座標を返す
		static int32_t __cdecl GetMousePositionInViewCallback(ManagedVector2* outPosition);
		// 名前からCollisionのマスクを取得する
		static uint32_t __cdecl GetCollisionTypeMaskByNameCallback(const char* name);
		// Lineの点列を置き換える
		static void __cdecl LineSetPointsCallback(ManagedNativeEntity entity, const ManagedLinePoint* points, int32_t count,
			int32_t loop);
		// Lineへ点を追加する
		static int32_t __cdecl LineAddPointCallback(ManagedNativeEntity entity, ManagedLinePoint point);
		// Lineの指定点を更新する
		static void __cdecl LineUpdatePointCallback(ManagedNativeEntity entity, ManagedLinePoint point);
		// Canvasの入力割当をコピーする
		static int32_t __cdecl CanvasCopyInputBindingsCallback(ManagedNativeEntity entity, int32_t action, int32_t device,
			int32_t* bindings, int32_t capacity);
		// Canvasの入力割当を設定する
		static void __cdecl CanvasSetInputBindingsCallback(ManagedNativeEntity entity, int32_t action, int32_t device,
			const int32_t* bindings, int32_t count);
		// Canvasの選択表の行数と列数を返す
		static int32_t __cdecl CanvasGetNavigationTableSizeCallback(ManagedNativeEntity entity, int32_t* outRows,
			int32_t* outColumns);
		// Canvasの選択表の行数と列数を変更する
		static int32_t __cdecl CanvasResizeNavigationTableCallback(ManagedNativeEntity entity, int32_t rows, int32_t columns);
		// Canvasの選択先Entityを返す
		static int32_t __cdecl CanvasGetNavigationCellCallback(ManagedNativeEntity entity, int32_t row, int32_t column,
			ManagedNativeEntity* outTarget);
		// Canvasの選択先Entityを設定する
		static int32_t __cdecl CanvasSetNavigationCellCallback(ManagedNativeEntity entity, int32_t row, int32_t column,
			ManagedNativeEntity target);
		// 画面座標をCanvas座標へ変換する
		static int32_t __cdecl CanvasScreenToLocalPointCallback(ManagedNativeEntity entity, ManagedVector2 screenPosition,
			ManagedVector2* outLocalPosition);
		// Particleの再生操作を予約する
		static void __cdecl ParticleSystemControlCallback(ManagedNativeEntity entity, int32_t operation, int32_t stopBehavior,
			int32_t withChildren);
		// Particleの実行状態を返す
		static int32_t __cdecl ParticleSystemStateCallback(ManagedNativeEntity entity, int32_t state, int32_t withChildren);
		// 点列から即時Lineを描く
		static void __cdecl LineDrawImmediateCallback(const ManagedLinePoint* points, int32_t count, int32_t loop,
			int32_t is2D, ManagedAssetGUID materialID);
		// 球形の即時Lineを描く
		static void __cdecl LineDrawSphereImmediateCallback(ManagedVector3 center, float radius, ManagedColor4 color,
			int32_t division, float thickness, ManagedAssetGUID materialID);
		// EntityのTagをコピーする
		static int32_t __cdecl CopyTagCallback(ManagedNativeEntity entity, char* buffer, int32_t capacity);
		// EntityのTagを設定する
		static void __cdecl SetTagCallback(ManagedNativeEntity entity, const char* tag);
		// 描画対象のLayerを返す
		static int32_t __cdecl GetVisibilityLayerMaskCallback(ManagedNativeEntity entity);
		// 描画対象のLayerを設定する
		static void __cdecl SetVisibilityLayerMaskCallback(ManagedNativeEntity entity, int32_t mask);
		// CollisionのLayerを返す
		static int32_t __cdecl GetCollisionTypeMaskCallback(ManagedNativeEntity entity);
		// Collisionの接触状態を返す
		static int32_t __cdecl GetCollisionRuntimeStateCallback(ManagedNativeEntity entity);
		// CollisionのLayerを設定する
		static void __cdecl SetCollisionTypeMaskCallback(ManagedNativeEntity entity, int32_t mask);
		// 名前でEntityを検索する
		static ManagedNativeEntity __cdecl FindEntityByNameCallback(const char* name);
		// TagでEntityを検索する
		static ManagedNativeEntity __cdecl FindEntityByTagCallback(const char* tag);
		// TagでEntity一覧を取得する
		static int32_t __cdecl FindEntitiesByTagCallback(const char* tag, ManagedNativeEntity* buffer, int32_t capacity);
		// ComponentでEntityを検索する
		static ManagedNativeEntity __cdecl FindEntityByComponentCallback(int32_t typeID);
		// ComponentでEntity一覧を取得する
		static int32_t __cdecl FindEntitiesByComponentCallback(int32_t typeID, ManagedNativeEntity* buffer, int32_t capacity);
		// 指定形状の即時Lineを描く
		static void __cdecl LineDrawShapeCallback(const ManagedLineShape* shape);
		// Sceneの解放を要求する
		static void __cdecl UnloadSceneCallback(uint64_t sceneInstanceID);
		// Scene実体の生存状態を返す
		static int32_t __cdecl IsSceneInstanceAliveCallback(uint64_t sceneInstanceID);
		// 座標保持を指定して親変更を予約する
		static void __cdecl SetParentKeepWorldCallback(ManagedNativeEntity child, ManagedNativeEntity parent,
			int32_t worldPositionStays);
		// 指定パッドのボタン状態を返す
		static int32_t __cdecl GetGamepadButtonIndexedCallback(int32_t index, int32_t button);
		// 指定パッドの押下開始を返す
		static int32_t __cdecl GetGamepadButtonDownIndexedCallback(int32_t index, int32_t button);
		// 指定パッドの解放を返す
		static int32_t __cdecl GetGamepadButtonUpIndexedCallback(int32_t index, int32_t button);
		// 指定パッドの軸値を返す
		static float __cdecl GetGamepadAxisCallback(int32_t index, int32_t axis);
		// 指定パッドの接続状態を返す
		static int32_t __cdecl IsGamepadConnectedIndexedCallback(int32_t index);
		// 接続中のパッド数を返す
		static int32_t __cdecl GetConnectedGamepadCountCallback();
		// Playerのパッド割当を返す
		static int32_t __cdecl GetPlayerGamepadIndexCallback(int32_t playerIndex);
		// Playerのキーとマウスの割当を返す
		static int32_t __cdecl GetPlayerKeyboardMouseEnabledCallback(int32_t playerIndex);
		// Playerの入力可否を返す
		static int32_t __cdecl GetPlayerInputAvailableCallback(int32_t playerIndex);
		// Playerのパッド振動を開始する
		static uint32_t __cdecl PlayPlayerVibrationCallback(int32_t playerIndex, float left, float right, float duration,
			float attack, float release);
		// Playerのパッド振動を止める
		static void __cdecl StopPlayerVibrationCallback(int32_t playerIndex, uint32_t handle);
		// アプリのフォーカス状態を返す
		static int32_t __cdecl GetHasFocusCallback();
		// 入力された文字列をコピーする
		static int32_t __cdecl CopyTextInputCallback(char* buffer, int32_t capacity);
		// Projectのルートをコピーする
		static int32_t __cdecl CopyProjectRootCallback(char* buffer, int32_t capacity);
		// ユーザー設定のルートをコピーする
		static int32_t __cdecl CopyUserSettingsRootCallback(char* buffer, int32_t capacity);
		// Audioの再生を開始する
		static void __cdecl AudioPlayCallback(ManagedNativeEntity entity);
		// AnimatorのParameterを設定する
		static int32_t __cdecl SetAnimatorParameterCallback(ManagedNativeEntity entity, const char* name, int32_t type,
			float number, int32_t integer);
		// AnimatorのParameterを取得する
		static int32_t __cdecl GetAnimatorParameterCallback(ManagedNativeEntity entity, const char* name, int32_t type,
			float* number, int32_t* integer);
		// Audioを一度だけ重ねて再生する
		static void __cdecl AudioPlayOneShotCallback(ManagedNativeEntity entity, ManagedAssetGUID clipID, float volumeScale);
		// Audioの再生を一時停止する
		static void __cdecl AudioPauseCallback(ManagedNativeEntity entity);
		// Audioの一時停止を解除する
		static void __cdecl AudioUnPauseCallback(ManagedNativeEntity entity);
		// Audioの再生を終了する
		static void __cdecl AudioStopCallback(ManagedNativeEntity entity);
		// Audioの再生状態を返す
		static int32_t __cdecl AudioIsPlayingCallback(ManagedNativeEntity entity);
		// Script例外をNativeへ報告する
		static void __cdecl ReportScriptExceptionCallback(const char* jsonUtf8);
		// 実行中の更新停止要求を返す
		static int32_t __cdecl IsUpdateInterruptedCallback();
		// 指定した補間曲線の値を返す
		static float __cdecl EasedValueCallback(int32_t easingType, float t);
	};

} // Engine
