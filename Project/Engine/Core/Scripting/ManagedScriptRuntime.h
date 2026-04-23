#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/ManagedScriptTypes.h>
#include <Engine/Core/ECS/Entity/Entity.h>

// c++
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
		//========================================================================
		//	public Methods
		//========================================================================

		ManagedScriptRuntime() = default;
		~ManagedScriptRuntime() = default;

		// .NETランタイムとスクリプトアセンブリの初期化
		bool Init();
		// 終了処理
		void Finalize();

		// C#スクリプト型をビヘイビアレジストリへ反映する
		void RefreshScriptTypes();

		// C#スクリプトのインスタンスを作成/破棄する
		int32_t CreateInstance(const std::string& typeName, ECSWorld& world,
			const Entity& entity, const nlohmann::json& serializedFields);
		void DestroyInstance(int32_t handle);

		// ライフサイクル呼び出し
		void InvokeAwake(int32_t handle, const SystemContext& context);
		void InvokeStart(int32_t handle, const SystemContext& context);
		void InvokeOnEnable(int32_t handle, const SystemContext& context);
		void InvokeOnDisable(int32_t handle, const SystemContext& context);
		void InvokeOnDestroy(int32_t handle, const SystemContext& context);
		void InvokeFixedUpdate(int32_t handle, const SystemContext& context);
		void InvokeUpdate(int32_t handle, const SystemContext& context);
		void InvokeLateUpdate(int32_t handle, const SystemContext& context);

		//--------- accessor -----------------------------------------------------

		bool IsInitialized() const { return initialized_; }
		bool TryResolveScriptTypeName(const std::string_view& scriptName, std::string& outTypeName) const;
		const std::vector<ManagedScriptField>& GetSerializedFields(const std::string& typeName);

		static ManagedScriptRuntime& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- types --------------------------------------------------------

		using HostfxrHandle = void*;
		using HostfxrInitializeForRuntimeConfigFn = int32_t(__cdecl*)(const wchar_t*, const void*, HostfxrHandle*);
		using HostfxrGetRuntimeDelegateFn = int32_t(__cdecl*)(HostfxrHandle, int32_t, void**);
		using HostfxrCloseFn = int32_t(__cdecl*)(HostfxrHandle);
		using LoadAssemblyAndGetFunctionPointerFn = int32_t(__cdecl*)(const wchar_t*, const wchar_t*,
			const wchar_t*, const wchar_t*, void*, void**);

		using InitializeNativeApiFn = int32_t(__cdecl*)(ManagedNativeApiTable*);
		using LoadGameAssemblyFn = int32_t(__cdecl*)(const char*);
		using GetScriptTypeCountFn = int32_t(__cdecl*)();
		using CopyScriptTypeNameFn = int32_t(__cdecl*)(int32_t, char*, int32_t);
		using GetSerializedFieldCountFn = int32_t(__cdecl*)(const char*);
		using CopySerializedFieldInfoFn = int32_t(__cdecl*)(const char*, int32_t, ManagedNativeSerializedFieldInfo*);
		using CreateInstanceFn = int32_t(__cdecl*)(const char*, ManagedNativeEntity, const char*);
		using DestroyInstanceFn = void(__cdecl*)(int32_t);
		using InvokeFn = void(__cdecl*)(int32_t);

		//--------- variables ----------------------------------------------------

		bool initialized_ = false;
		void* hostfxrLibrary_ = nullptr;

		HostfxrCloseFn hostfxrClose_ = nullptr;
		LoadAssemblyAndGetFunctionPointerFn loadAssemblyAndGetFunctionPointer_ = nullptr;

		InitializeNativeApiFn initializeNativeApi_ = nullptr;
		LoadGameAssemblyFn loadGameAssembly_ = nullptr;
		GetScriptTypeCountFn getScriptTypeCount_ = nullptr;
		CopyScriptTypeNameFn copyScriptTypeName_ = nullptr;
		GetSerializedFieldCountFn getSerializedFieldCount_ = nullptr;
		CopySerializedFieldInfoFn copySerializedFieldInfo_ = nullptr;
		CreateInstanceFn createInstance_ = nullptr;
		DestroyInstanceFn destroyInstance_ = nullptr;
		InvokeFn invokeAwake_ = nullptr;
		InvokeFn invokeStart_ = nullptr;
		InvokeFn invokeOnEnable_ = nullptr;
		InvokeFn invokeOnDisable_ = nullptr;
		InvokeFn invokeOnDestroy_ = nullptr;
		InvokeFn invokeFixedUpdate_ = nullptr;
		InvokeFn invokeUpdate_ = nullptr;
		InvokeFn invokeLateUpdate_ = nullptr;

		const SystemContext* currentContext_ = nullptr;

		std::filesystem::path scriptCoreAssemblyPath_;
		std::filesystem::path gameAssemblyPath_;
		std::unordered_map<std::string, std::vector<ManagedScriptField>> fieldCache_;

		//--------- functions ----------------------------------------------------

		bool LoadHostfxr();
		bool InitRuntime();
		bool LoadBridgeFunctions();
		bool LoadGameAssembly();
		void ReleaseHostfxr();

		template <typename T>
		bool LoadBridgeFunction(T& outFunction, const wchar_t* methodName);

		void Invoke(InvokeFn function, int32_t handle, const SystemContext& context);

		// C#へ渡すコールバック
		static float __cdecl GetDeltaTimeCallback();
		static ManagedVector3 __cdecl GetPositionCallback(ManagedNativeEntity entity);
		static void __cdecl SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value);
		static ManagedVector3 __cdecl GetLocalPositionCallback(ManagedNativeEntity entity);
		static void __cdecl SetLocalPositionCallback(ManagedNativeEntity entity, ManagedVector3 value);
		static ManagedVector3 __cdecl GetLocalScaleCallback(ManagedNativeEntity entity);
		static void __cdecl SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value);
		static ManagedQuaternion __cdecl GetLocalRotationCallback(ManagedNativeEntity entity);
		static void __cdecl SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value);
	};

	//============================================================================
	//	ManagedScriptRuntime templateMethods
	//============================================================================

	template <typename T>
	inline bool ManagedScriptRuntime::LoadBridgeFunction(T& outFunction, const wchar_t* methodName) {

		void* function = nullptr;
		const wchar_t* typeName = L"NEMEngine.HostBridge, NEM.ScriptCore";
		const wchar_t* unmanagedCallersOnly = reinterpret_cast<const wchar_t*>(-1);

		int32_t result = loadAssemblyAndGetFunctionPointer_(
			scriptCoreAssemblyPath_.c_str(), typeName, methodName, unmanagedCallersOnly, nullptr, &function);
		if (result != 0 || !function) {
			outFunction = nullptr;
			return false;
		}
		outFunction = reinterpret_cast<T>(function);
		return true;
	}
} // Engine
