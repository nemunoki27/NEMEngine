#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Scripting/Managed/DotnetHostResolver.h>
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
		// ゲーム側C#アセンブリを読み直す（ResolveGameAssemblyPathの現行ビルド出力をロード）
		bool ReloadGameAssembly(bool waitForManagedDebugger = false);
		// 指定したdllを明示的にロードする（Edit reloadのshadow copyロード用）。unload→load→型反映
		bool LoadGameAssemblyFromPath(const std::filesystem::path& dllPath, bool waitForManagedDebugger = false);
		// ゲーム側C#アセンブリを解放する
		void UnloadGameAssembly();

		// GameScripts.csproj のパスを解決する（存在しなければ空）。Edit build serviceが使う
		std::filesystem::path GameScriptProjectPath() const;
		// 現在ロード中のGameScripts.dllのパス（last-known-goodのseed等に使う）
		const std::filesystem::path& ActiveAssemblyPath() const { return gameAssemblyPath_; }
		// 直近のRefreshScriptTypesで反映したmanaged script型数（reload診断用）
		int32_t ManagedScriptTypeCount() const { return lastManagedTypeCount_; }

		// Stable Script Type GUID から script instanceを作成する。生成失敗時は無効ハンドルを返す。
		// scriptSlotId は ScriptBehaviour.Enabled が owner+slot で自身の runtime entry を特定するために C# へ渡す
		ManagedScriptInstanceHandle CreateInstance(const std::string& scriptTypeId, ECSWorld& world,
			const Entity& entity, const nlohmann::json& serializedFields, uint64_t scriptSlotId);
		// 対象DLLを検証してScript Manifest(JSON)を生成する（build/reload時のみ。現行DLLは触らない）
		ManagedStatus GenerateScriptManifest(const std::filesystem::path& assemblyPath,
			const std::filesystem::path& manifestOutputPath);
		void SetSerializedFields(ManagedScriptInstanceHandle handle, const nlohmann::json& serializedFields);
		void DestroyInstance(ManagedScriptInstanceHandle handle);

		// ライフサイクル呼び出し。C#側で例外を封じ込めた結果をManagedStatusで返す
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
		// Stable Script Type GUID に対応する serialized field schema を取得する（Inspector描画用）。
		// blob で受け取り一度だけ parse して cache する（reload で破棄）。未解決は空 schema を返す
		const ManagedScriptSchema& GetScriptSchema(const std::string& scriptTypeId);
		// authoring serializedFields（任意形式）から { fieldGuid: value } の値マップを作る（migration 込み）
		nlohmann::json BuildSerializedValueMap(const std::string& scriptTypeId, const nlohmann::json& serializedFields);

		// Play 中 runtime Inspector 用：instance の現在値を { fieldGuid: value } で取得する
		nlohmann::json GetRuntimeSerializedState(ManagedScriptInstanceHandle handle);
		// runtime instance の単一 field を即時更新する（authoring へは保存しない）
		void SetRuntimeSerializedField(ManagedScriptInstanceHandle handle, const std::string& fieldId, const nlohmann::json& value);

		// 現在のライフサイクル呼び出しのコンテキスト（main threadのcallbackから参照する）
		static const SystemContext* GetCurrentContext();

		static ManagedScriptRuntime& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- types --------------------------------------------------------

		// load_assembly_and_get_function_pointer デリゲートのシグネチャ。
		// hostfxr/coreclr_delegatesの取得・RAII管理はDotnetHostResolverへ分離した。
		// (x64ではcoreclr_delegatesの__stdcallと__cdeclは同一ABIのためここで呼び出し可能)
		using LoadAssemblyAndGetFunctionPointerFn = int32_t(__cdecl*)(const wchar_t*, const wchar_t*,
			const wchar_t*, const wchar_t*, void*, void**);

		// 全exportは例外を境界外へ出さず、結果をManagedStatusで返す。
		// 値を返すAPIは ManagedStatus + out parameter 形式にする
		using InitializeNativeApiFn = ManagedStatus(__cdecl*)(ManagedNativeApiTable*);
		using LoadGameAssemblyFn = ManagedStatus(__cdecl*)(const char*);
		using UnloadGameAssemblyFn = ManagedStatus(__cdecl*)();
		using GetScriptTypeCountFn = ManagedStatus(__cdecl*)(int32_t*);
		using CopyScriptTypeInfoFn = ManagedStatus(__cdecl*)(int32_t, ManagedScriptTypeDescriptor*);
		using GenerateScriptManifestFn = ManagedStatus(__cdecl*)(const char*, const char*);
		// 二段階 blob schema API（固定長 buffer を使わない）
		using GetScriptSchemaJsonSizeFn = ManagedStatus(__cdecl*)(const char*, int32_t*);
		using CopyScriptSchemaJsonFn = ManagedStatus(__cdecl*)(const char*, char*, int32_t, int32_t*);
		// Play 中 runtime Inspector のための instance 値 readback / set
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

		// ライフサイクル呼び出し中だけ有効なコンテキスト。
		// thread_localにし、ネスト呼び出しや例外/早期returnでも確実に復元する
		static thread_local const SystemContext* currentContext_;

		std::filesystem::path scriptCoreAssemblyPath_;
		std::filesystem::path gameAssemblyPath_;
		// Stable Script Type GUID -> parse 済み schema（reload で破棄）
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
		// generic component access / Entity 破棄 / ScriptBehaviour.Enabled
		static int32_t __cdecl GetComponentTypeIdCallback(const char* name);
		static int32_t __cdecl HasComponentCallback(ManagedNativeEntity entity, int32_t typeId);
		static void __cdecl AddComponentCallback(ManagedNativeEntity entity, int32_t typeId);
		static void __cdecl RemoveComponentCallback(ManagedNativeEntity entity, int32_t typeId);
		static void __cdecl DestroyEntityCallback(ManagedNativeEntity entity);
		static int32_t __cdecl GetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotId);
		static void __cdecl SetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotId, int32_t enabled);
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

