#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Scripting/Managed/DotnetHostResolver.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedAlcStatus.h>
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

	// front
	class ECSWorld;
	struct SystemContext;

	//============================================================================
	//	ManagedScriptRuntime class
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
		// ゲーム側C#アセンブリを読み直す、ResolveGameAssemblyPathの現行ビルド出力をロードする
		bool ReloadGameAssembly(bool waitForManagedDebugger = false);
		// 指定したdllを明示的にロードする、Edit reloadのshadow copyロード用でunloadしてからloadし型反映する
		bool LoadGameAssemblyFromPath(const std::filesystem::path& dllPath, bool waitForManagedDebugger = false);
		// ゲーム側C#アセンブリを解放する
		void UnloadGameAssembly();

		// GameScripts.csprojのパスを解決する、存在しなければ空でEdit build serviceが使う
		std::filesystem::path GameScriptProjectPath() const;
		// 現在ロード中のGameScripts.dllのパスでlast-known-goodのseed等に使う
		const std::filesystem::path& ActiveAssemblyPath() const { return gameAssemblyPath_; }
		// 直近のRefreshScriptTypesで反映したmanaged script型数でreload診断用
		int32_t ManagedScriptTypeCount() const { return lastManagedTypeCount_; }

		// Stable Script Type GUIDからinstanceを作成し失敗時は無効ハンドル、scriptSlotIDはC#がruntime entry特定に使う
		ManagedScriptInstanceHandle CreateInstance(const std::string& scriptTypeID, ECSWorld& world,
			const Entity& entity, const nlohmann::json& serializedFields, uint64_t scriptSlotID);
		// 対象DLLを検証してScript ManifestのJSONを生成する、buildやreload時のみで現行DLLは触らない
		ManagedStatus GenerateScriptManifest(const std::filesystem::path& assemblyPath,
			const std::filesystem::path& manifestOutputPath);
		void SetSerializedFields(ManagedScriptInstanceHandle handle, const nlohmann::json& serializedFields);
		void DestroyInstance(ManagedScriptInstanceHandle handle);

		// ライフサイクル呼び出しで、C#側で例外を封じ込めた結果をManagedStatusで返す
		ManagedStatus InvokeAwake(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeStart(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeOnEnable(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeOnDisable(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeOnDestroy(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeFixedUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context);
		ManagedStatus InvokeLateUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context);

		// C#側のOnCollisionEnterを呼び出す
		ManagedStatus InvokeCollisionEnter(ManagedScriptInstanceHandle handle, const SystemContext& context, const ManagedCollisionEvent& collision);
		// C#側のOnCollisionStayを呼び出す
		ManagedStatus InvokeCollisionStay(ManagedScriptInstanceHandle handle, const SystemContext& context, const ManagedCollisionEvent& collision);
		// C#側のOnCollisionExitを呼び出す
		ManagedStatus InvokeCollisionExit(ManagedScriptInstanceHandle handle, const SystemContext& context, const ManagedCollisionEvent& collision);

		//--------- accessor -----------------------------------------------------

		bool IsInitialized() const { return initialized_; }
		// Inspector描画用にserialized field schemaを取得する、blobを一度だけparseしてcacheし未解決は空schema
		const ManagedScriptSchema& GetScriptSchema(const std::string& scriptTypeID);
		// 任意形式のauthoring serializedFieldsからfieldGuidとvalueの値マップをmigration込みで作る
		nlohmann::json BuildSerializedValueMap(const std::string& scriptTypeID, const nlohmann::json& serializedFields);

		// Play中runtime Inspector用：instanceの現在値を{ fieldGuid: value }で取得する
		nlohmann::json GetRuntimeSerializedState(ManagedScriptInstanceHandle handle);
		// runtime instanceの単一fieldを即時更新する、authoringへは保存しない
		void SetRuntimeSerializedField(ManagedScriptInstanceHandle handle, const std::string& fieldID, const nlohmann::json& value);

		// 現在のライフサイクル呼び出しのコンテキストでmain threadのcallbackから参照する
		static const SystemContext* GetCurrentContext();

		//--------- gameplay time service ----------------------------------------

		// BehaviorSystemのSynchronizeLifecycle Pass4から呼び、SceneとApplicationイベントをC#側でpumpする
		void PumpSceneEvents();
		// application shutdown前に一度だけ呼び、C# Application.Quittingを発火する
		void RaiseApplicationQuitting();
		// 直近のcollectible ALC unloadのtyped statusを返す、reload後にEditorが参照する
		AlcUnloadStatus GetLastAlcUnloadStatus();
		// 各phase末から呼ぶper-frame tickでphaseは0がUpdate 1がFixedUpdate 2がEndOfFrame、TimerとCoroutineを駆動する
		void TickFrame(int32_t phase, const SystemContext& context);

		// Play開始時に時間状態を初期化し、worldのTimeScaleComponentがあれば初期scaleを読む
		static void BeginPlayTime(ECSWorld* playWorld);
		// 1フレーム分の時間を進める、advancingがfalseのEditや停止中は累積しない、返り値はscale適用後のdeltaTime
		static float AdvanceTime(float rawDeltaTime, float fixedDeltaTime, bool advancing);

		static ManagedScriptRuntime& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- types --------------------------------------------------------

		// load_assembly_and_get_function_pointerデリゲートのシグネチャ、x64では__stdcallと__cdeclが同一ABIで呼び出し可能
		using LoadAssemblyAndGetFunctionPointerFn = int32_t(__cdecl*)(const wchar_t*, const wchar_t*,
			const wchar_t*, const wchar_t*, void*, void**);

		// 全exportは例外を境界外へ出さずManagedStatusで返し、値を返すAPIはout parameter形式にする
		using InitializeNativeApiFn = ManagedStatus(__cdecl*)(ManagedNativeApiTable*);
		using LoadGameAssemblyFn = ManagedStatus(__cdecl*)(const char*);
		using UnloadGameAssemblyFn = ManagedStatus(__cdecl*)();
		using GetScriptTypeCountFn = ManagedStatus(__cdecl*)(int32_t*);
		using CopyScriptTypeInfoFn = ManagedStatus(__cdecl*)(int32_t, ManagedScriptTypeDescriptor*);
		using GenerateScriptManifestFn = ManagedStatus(__cdecl*)(const char*, const char*);
		// 二段階blob schema APIで固定長bufferを使わない
		using GetScriptSchemaJsonSizeFn = ManagedStatus(__cdecl*)(const char*, int32_t*);
		using CopyScriptSchemaJsonFn = ManagedStatus(__cdecl*)(const char*, char*, int32_t, int32_t*);
		// Play中runtime Inspectorのためのinstance値readback / set
		using GetRuntimeStateSizeFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, int32_t*);
		using CopyRuntimeStateFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, char*, int32_t, int32_t*);
		using SetRuntimeFieldFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, const char*, const char*);
		using CreateInstanceFn = ManagedStatus(__cdecl*)(const char*, ManagedNativeEntity, const char*, uint64_t, ManagedScriptInstanceHandle*);
		using SetSerializedFieldsFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, const char*);
		using DestroyInstanceFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle);
		using InvokeFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle);
		using InvokeCollisionFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, ManagedCollisionEvent);

		//--------- variables ----------------------------------------------------

		bool initialized_ = false;

		// hostfxrの探索・ロード・デリゲート取得をRAIIで管理するサービス
		DotnetHostResolver dotnetHost_;

		InitializeNativeApiFn initializeNativeApi_ = nullptr;
		LoadGameAssemblyFn loadGameAssembly_ = nullptr;
		UnloadGameAssemblyFn unloadGameAssembly_ = nullptr;
		// SceneイベントpumpのPumpSceneEventsでUnloadGameAssemblyFnと同じ無引数シグネチャ
		UnloadGameAssemblyFn pumpSceneEvents_ = nullptr;
		// application終了通知のRaiseApplicationQuittingで同じ無引数シグネチャ
		UnloadGameAssemblyFn raiseApplicationQuitting_ = nullptr;
		// per-frame tickのTickFrameでphaseを引数に取る
		using TickFrameFn = ManagedStatus(__cdecl*)(int32_t);
		TickFrameFn tickFrame_ = nullptr;
		// 直近ALC unload statusのGetLastAlcUnloadStatusでintを返す無引数
		using IntNoArgFn = int32_t(__cdecl*)();
		IntNoArgFn getLastAlcUnloadStatus_ = nullptr;
		GetScriptTypeCountFn getScriptTypeCount_ = nullptr;
		CopyScriptTypeInfoFn copyScriptTypeInfo_ = nullptr;
		GenerateScriptManifestFn generateScriptManifest_ = nullptr;
		GetScriptSchemaJsonSizeFn getScriptSchemaJsonSize_ = nullptr;
		CopyScriptSchemaJsonFn copyScriptSchemaJson_ = nullptr;
		GetRuntimeStateSizeFn getRuntimeStateSize_ = nullptr;
		CopyRuntimeStateFn copyRuntimeState_ = nullptr;
		SetRuntimeFieldFn setRuntimeField_ = nullptr;
		CreateInstanceFn createInstance_ = nullptr;
		SetSerializedFieldsFn setSerializedFields_ = nullptr;
		DestroyInstanceFn destroyInstance_ = nullptr;
		InvokeFn invokeAwake_ = nullptr;
		InvokeFn invokeStart_ = nullptr;
		InvokeFn invokeOnEnable_ = nullptr;
		InvokeFn invokeOnDisable_ = nullptr;
		InvokeFn invokeOnDestroy_ = nullptr;
		InvokeFn invokeFixedUpdate_ = nullptr;
		InvokeFn invokeUpdate_ = nullptr;
		InvokeFn invokeLateUpdate_ = nullptr;

		// Collisionイベント呼び出し関数
		InvokeCollisionFn invokeCollisionEnter_ = nullptr;
		InvokeCollisionFn invokeCollisionStay_ = nullptr;
		InvokeCollisionFn invokeCollisionExit_ = nullptr;

		// ライフサイクル呼び出し中だけ有効なthread_localコンテキストで、ネストや例外や早期returnでも確実に復元する
		static thread_local const SystemContext* currentContext_;

		// gameplay time serviceの状態でmain threadのみ更新、scaleはserviceがauthorityでTimeScaleComponentはseedのみ
		static float timeScale_;
		static float scaledDeltaTime_;
		static float unscaledDeltaTime_;
		static float fixedDeltaTime_;
		static double timeSinceStartup_;
		static double unscaledTime_;
		static uint64_t frameCount_;

		std::filesystem::path scriptCoreAssemblyPath_;
		std::filesystem::path gameAssemblyPath_;
		// Stable Script Type GUIDからparse済みschemaへのマップでreloadで破棄する
		std::unordered_map<std::string, ManagedScriptSchema> schemaCache_;
		// 直近のRefreshScriptTypesで反映したmanaged script型数
		int32_t lastManagedTypeCount_ = 0;

		//--------- functions ----------------------------------------------------

		bool LoadHostfxr();
		bool LoadBridgeFunctions();
		bool LoadGameAssembly();
		void ReleaseHostfxr();

		template <typename T>
		bool LoadBridgeFunction(T& outFunction, const wchar_t* methodName);

		ManagedStatus Invoke(InvokeFn function, ManagedScriptInstanceHandle handle, const SystemContext& context);
		// C#側のCollisionイベント関数を呼び出す
		ManagedStatus InvokeCollision(InvokeCollisionFn function, ManagedScriptInstanceHandle handle,
			const SystemContext& context, const ManagedCollisionEvent& collision);

		//============================================================================
		//	ScopedInvocationContext
		//	currentContext_をRAIIで一時設定し、scope離脱時に必ず元へ戻す
		//============================================================================
		class ScopedInvocationContext {
		public:
			explicit ScopedInvocationContext(const SystemContext& context);
			~ScopedInvocationContext();

			ScopedInvocationContext(const ScopedInvocationContext&) = delete;
			ScopedInvocationContext& operator=(const ScopedInvocationContext&) = delete;
		private:
			const SystemContext* previous_;
		};

		// C#へ渡すコールバック
		static float __cdecl GetDeltaTimeCallback();
		static float __cdecl GetFixedDeltaTimeCallback();
		static void __cdecl LogCallback(int32_t level, const char* message);
		static int32_t __cdecl GetKeyCallback(int32_t key);
		static int32_t __cdecl GetKeyDownCallback(int32_t key);
		static int32_t __cdecl GetKeyUpCallback(int32_t key);
		static int32_t __cdecl GetMouseButtonCallback(int32_t button);
		static int32_t __cdecl GetMouseButtonDownCallback(int32_t button);
		static int32_t __cdecl GetMouseButtonUpCallback(int32_t button);
		static ManagedVector2 __cdecl GetMousePositionCallback();
		static ManagedVector2 __cdecl GetMouseDeltaCallback();
		static float __cdecl GetMouseWheelCallback();
		static int32_t __cdecl GetGamepadButtonCallback(int32_t button);
		static int32_t __cdecl GetGamepadButtonDownCallback(int32_t button);
		static int32_t __cdecl IsGamepadConnectedCallback();
		static ManagedVector2 __cdecl GetLeftStickCallback();
		static ManagedVector2 __cdecl GetRightStickCallback();
		static float __cdecl GetLeftTriggerCallback();
		static float __cdecl GetRightTriggerCallback();
		static int32_t __cdecl IsAliveCallback(ManagedNativeEntity entity);
		static int32_t __cdecl CopyNameCallback(ManagedNativeEntity entity, char* buffer, int32_t capacity);
		static void __cdecl SetNameCallback(ManagedNativeEntity entity, const char* name);
		static int32_t __cdecl GetActiveSelfCallback(ManagedNativeEntity entity);
		static void __cdecl SetActiveSelfCallback(ManagedNativeEntity entity, int32_t active);
		static int32_t __cdecl GetActiveInHierarchyCallback(ManagedNativeEntity entity);
		static ManagedNativeEntity __cdecl GetParentCallback(ManagedNativeEntity entity);
		static ManagedNativeEntity __cdecl GetFirstChildCallback(ManagedNativeEntity entity);
		static ManagedNativeEntity __cdecl GetNextSiblingCallback(ManagedNativeEntity entity);
		static void __cdecl SetParentCallback(ManagedNativeEntity entity, ManagedNativeEntity parent);
		static ManagedVector3 __cdecl GetPositionCallback(ManagedNativeEntity entity);
		static void __cdecl SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value);
		static ManagedVector3 __cdecl GetLocalPositionCallback(ManagedNativeEntity entity);
		static void __cdecl SetLocalPositionCallback(ManagedNativeEntity entity, ManagedVector3 value);
		static ManagedVector3 __cdecl GetLocalScaleCallback(ManagedNativeEntity entity);
		static void __cdecl SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value);
		static ManagedQuaternion __cdecl GetLocalRotationCallback(ManagedNativeEntity entity);
		static void __cdecl SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value);
		static ManagedQuaternion __cdecl GetRotationCallback(ManagedNativeEntity entity);
		static void __cdecl SetRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value);
		static ManagedVector3 __cdecl GetLossyScaleCallback(ManagedNativeEntity entity);
		// v16のTransform親追従の継承フラグ、回転とスケールを任意で無視する
		static int32_t __cdecl GetIgnoreParentRotationCallback(ManagedNativeEntity entity);
		static void __cdecl SetIgnoreParentRotationCallback(ManagedNativeEntity entity, int32_t value);
		static int32_t __cdecl GetIgnoreParentScaleCallback(ManagedNativeEntity entity);
		static void __cdecl SetIgnoreParentScaleCallback(ManagedNativeEntity entity, int32_t value);
		// generic component access / Entity破棄/ ScriptBehaviour.Enabled
		static int32_t __cdecl GetComponentTypeIdCallback(const char* name);
		static int32_t __cdecl HasComponentCallback(ManagedNativeEntity entity, int32_t typeID);
		static void __cdecl AddComponentCallback(ManagedNativeEntity entity, int32_t typeID);
		static void __cdecl RemoveComponentCallback(ManagedNativeEntity entity, int32_t typeID);
		static void __cdecl DestroyEntityCallback(ManagedNativeEntity entity);
		static int32_t __cdecl GetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID);
		static void __cdecl SetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID, int32_t enabled);
		// GetComponent<Script> v9でowner Entity上のscriptTypeID一致instanceハンドルを返す
		static ManagedScriptInstanceHandle __cdecl GetScriptInstanceCallback(ManagedNativeEntity owner, const char* scriptTypeID);
		// AddComponent<Script> v22でowner EntityへscriptTypeIDのscriptをruntime attachする
		static int32_t __cdecl AttachScriptCallback(ManagedNativeEntity owner, const char* scriptTypeID);
		// Gameplay v7のTime拡張とTimeScaleでscaledはgetDeltaTimeとgetFixedDeltaTimeが返す既存値
		static float __cdecl GetUnscaledDeltaTimeCallback();
		static float __cdecl GetUnscaledFixedDeltaTimeCallback();
		static double __cdecl GetTimeSinceStartupCallback();
		static double __cdecl GetUnscaledTimeCallback();
		static float __cdecl GetTimeScaleCallback();
		static void __cdecl SetTimeScaleCallback(float value);
		static uint64_t __cdecl GetFrameCountCallback();

		// v17の入力デバイス、入力タイプとマウス範囲制御の取得設定
		static int32_t __cdecl GetInputTypeCallback();
		static void __cdecl SetInputTypeCallback(int32_t type);
		static int32_t __cdecl GetMouseRangeControlCallback();
		static void __cdecl SetMouseRangeControlCallback(int32_t enabled);
		// Mesh/Sprite/Textのマテリアルcolorを上書きする、componentType 0=Mesh 1=Sprite 2=Text、subMeshIndex<0で全サブメッシュ
		static void __cdecl SetRendererMaterialColorCallback(ManagedNativeEntity entity, int32_t componentType,
			int32_t subMeshIndex, const char* param, float r, float g, float b, float a);
		// Mesh/Sprite/Textのマテリアルcolorを取得する、未設定は白を返す
		static ManagedColor4 __cdecl GetRendererMaterialColorCallback(ManagedNativeEntity entity,
			int32_t componentType, int32_t subMeshIndex);
		// Gameplay v7のAssetRef runtime resolve
		static int32_t __cdecl AssetExistsCallback(uint64_t assetID);
		static int32_t __cdecl CopyAssetDisplayNameCallback(uint64_t assetID, char* buffer, int32_t capacity);
		// Gameplay v7のEntity生成とPrefabとSceneとSetParentのworldPositionStays
		static ManagedNativeEntity __cdecl CreateEntityCallback(const char* name, ManagedNativeEntity parent);
		static ManagedNativeEntity __cdecl InstantiatePrefabCallback(uint64_t prefabAssetID, ManagedVector3 position, ManagedQuaternion rotation, int32_t useTransform, ManagedNativeEntity parent);
		static uint64_t __cdecl LoadSceneAdditiveCallback(uint64_t sceneAssetID);
		static uint64_t __cdecl LoadSceneSingleCallback(uint64_t sceneAssetID);
		// EntityRefをlocalFileIDからruntime entityへ解決する、対象が無ければNull
		static ManagedNativeEntity __cdecl ResolveEntityRefCallback(uint64_t sourceAsset, uint64_t localFileID);
		// EntityのSceneObject識別子を逆引きする、参照フィールドの保存表現に使う
		static void __cdecl GetEntityReferenceIdentityCallback(ManagedNativeEntity entity, uint64_t* sourceAsset, uint64_t* localFileID, int32_t* kind);
		// レイキャストの最近ヒットを返す、ヒット無しは0
		static int32_t __cdecl PhysicsRaycastCallback(ManagedVector3 origin, ManagedVector3 direction, float maxDistance, uint32_t layerMask, uint32_t targets, ManagedRaycastHit* outHit);
		// レイキャストの全ヒットを距離昇順で書き込みヒット総数を返す、bufferへはcapacity分だけ書く
		static int32_t __cdecl PhysicsRaycastAllCallback(ManagedVector3 origin, ManagedVector3 direction, float maxDistance, uint32_t layerMask, uint32_t targets, ManagedRaycastHit* buffer, int32_t capacity);
		// GameViewピクセル座標からワールドレイを作る、カメラ未解決は0
		static int32_t __cdecl ScreenPointToRayCallback(float x, float y, ManagedVector3* outOrigin, ManagedVector3* outDirection);
		// GameView内のマウス座標を描画解像度基準で返す、View外は0
		static int32_t __cdecl GetMousePositionInViewCallback(ManagedVector2* outPosition);
		// Collisionタイプ名からビットマスクを引く、未登録は0
		static uint32_t __cdecl GetCollisionTypeMaskByNameCallback(const char* name);
		// ライン描画v12でLineRendererComponentの点列を置き換える、count0でクリア
		static void __cdecl LineSetPointsCallback(ManagedNativeEntity entity, const ManagedLinePoint* points, int32_t count, int32_t loop);
		// LineRendererComponentの末尾へ1点追加し、採番したindexを返す
		static int32_t __cdecl LineAddPointCallback(ManagedNativeEntity entity, ManagedLinePoint point);
		// LineRendererComponentのpoint.indexの点を更新する、範囲外は何もしない
		static void __cdecl LineUpdatePointCallback(ManagedNativeEntity entity, ManagedLinePoint point);
		// FillMeshRendererComponentの点列を置き換える、count0でクリア
		static void __cdecl FillMeshSetPositionsCallback(ManagedNativeEntity entity, const ManagedVector3* points, int32_t count);
		// 即時ライン描画、任意ポリラインをこのフレームだけ描く
		static void __cdecl LineDrawImmediateCallback(const ManagedLinePoint* points, int32_t count, int32_t loop, int32_t is2D, uint64_t materialID);
		// 即時球描画、組み込みの球生成で線分を発行する
		static void __cdecl LineDrawSphereImmediateCallback(ManagedVector3 center, float radius, ManagedColor4 color, int32_t division, float thickness, uint64_t materialID);
		// v14のTag公開とLayerマスク公開と検索
		static int32_t __cdecl CopyTagCallback(ManagedNativeEntity entity, char* buffer, int32_t capacity);
		static void __cdecl SetTagCallback(ManagedNativeEntity entity, const char* tag);
		static int32_t __cdecl GetVisibilityLayerMaskCallback(ManagedNativeEntity entity);
		static void __cdecl SetVisibilityLayerMaskCallback(ManagedNativeEntity entity, int32_t mask);
		static int32_t __cdecl GetCollisionTypeMaskCallback(ManagedNativeEntity entity);
		static void __cdecl SetCollisionTypeMaskCallback(ManagedNativeEntity entity, int32_t mask);
		static ManagedNativeEntity __cdecl FindEntityByNameCallback(const char* name);
		static ManagedNativeEntity __cdecl FindEntityByTagCallback(const char* tag);
		static int32_t __cdecl FindEntitiesByTagCallback(const char* tag, ManagedNativeEntity* buffer, int32_t capacity);
		static ManagedNativeEntity __cdecl FindEntityByComponentCallback(int32_t typeID);
		static int32_t __cdecl FindEntitiesByComponentCallback(int32_t typeID, ManagedNativeEntity* buffer, int32_t capacity);
		// v15の即時形状描画、記述子から線分を生成して即時バッファへ積む
		static void __cdecl LineDrawShapeCallback(const ManagedLineShape* shape);
		static void __cdecl UnloadSceneCallback(uint64_t sceneInstanceID);
		static int32_t __cdecl IsSceneInstanceAliveCallback(uint64_t sceneInstanceID);
		static void __cdecl SetParentKeepWorldCallback(ManagedNativeEntity child, ManagedNativeEntity parent, int32_t worldPositionStays);
		// Gameplay v7のraw Input拡張多gamepadとaxisとtextとfocus
		static int32_t __cdecl GetGamepadButtonIndexedCallback(int32_t index, int32_t button);
		static int32_t __cdecl GetGamepadButtonDownIndexedCallback(int32_t index, int32_t button);
		static int32_t __cdecl GetGamepadButtonUpIndexedCallback(int32_t index, int32_t button);
		static float __cdecl GetGamepadAxisCallback(int32_t index, int32_t axis);
		static int32_t __cdecl IsGamepadConnectedIndexedCallback(int32_t index);
		static int32_t __cdecl GetConnectedGamepadCountCallback();
		static int32_t __cdecl GetHasFocusCallback();
		static int32_t __cdecl CopyTextInputCallback(char* buffer, int32_t capacity);
		// Gameplay v7のproject rootパス
		static int32_t __cdecl CopyProjectRootCallback(char* buffer, int32_t capacity);
		// Gameplay v7のAudioSource gameplay method
		static void __cdecl AudioPlayCallback(ManagedNativeEntity entity);
		static void __cdecl AudioPauseCallback(ManagedNativeEntity entity);
		static void __cdecl AudioStopCallback(ManagedNativeEntity entity);
		static int32_t __cdecl AudioIsPlayingCallback(ManagedNativeEntity entity);
		// Diagnostics v8のscript callback例外の構造化報告でJSON DTOをexception storeへ渡す
		static void __cdecl ReportScriptExceptionCallback(const char* jsonUtf8);
	};

	//============================================================================
	//	ManagedScriptRuntime templateMethods
	//============================================================================
	template <typename T>
	inline bool ManagedScriptRuntime::LoadBridgeFunction(T& outFunction, const wchar_t* methodName) {

		auto loadAssemblyAndGetFunctionPointer =
			reinterpret_cast<LoadAssemblyAndGetFunctionPointerFn>(dotnetHost_.GetLoadAssemblyDelegate());
		if (!loadAssemblyAndGetFunctionPointer) {
			outFunction = nullptr;
			return false;
		}

		void* function = nullptr;
		const wchar_t* typeName = L"NEMEngine.HostBridge, NEM.ScriptCore";
		const wchar_t* unmanagedCallersOnly = reinterpret_cast<const wchar_t*>(-1);

		int32_t result = loadAssemblyAndGetFunctionPointer(
			scriptCoreAssemblyPath_.c_str(), typeName, methodName, unmanagedCallersOnly, nullptr, &function);
		if (result != 0 || !function) {
			outFunction = nullptr;
			return false;
		}
		outFunction = reinterpret_cast<T>(function);
		return true;
	}
} // Engine

