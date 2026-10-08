#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Scripting/Managed/DotnetHostResolver.h>

namespace Engine {

	//============================================================================
	//	ManagedBridgeExports
	//	接続済みManaged関数と取得処理
	//============================================================================
	struct ManagedBridgeExports {

		// x64用のAssembly読込接続
		using LoadAssemblyAndGetFunctionPointerFn = int32_t(__cdecl*)(const wchar_t*, const wchar_t*,
			const wchar_t*, const wchar_t*, void*, void**);

		// 実行結果と出力値を分ける接続
		using InitializeNativeAPIFn = ManagedStatus(__cdecl*)(ManagedNativeAPITable*);
		using LoadGameAssemblyFn = ManagedStatus(__cdecl*)(const char*);
		using UnloadGameAssemblyFn = ManagedStatus(__cdecl*)();
		using GetScriptTypeCountFn = ManagedStatus(__cdecl*)(int32_t*);
		using CopyScriptTypeInfoFn = ManagedStatus(__cdecl*)(int32_t, ManagedScriptTypeDescriptor*);
		using GenerateScriptManifestFn = ManagedStatus(__cdecl*)(const char*, const char*);
		// Field情報をサイズ取得後にコピーする接続
		using GetScriptSchemaJsonSizeFn = ManagedStatus(__cdecl*)(const char*, int32_t*);
		using CopyScriptSchemaJsonFn = ManagedStatus(__cdecl*)(const char*, char*, int32_t, int32_t*);
		// 実行中のField値を読み書きする接続
		using GetRuntimeStateSizeFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, int32_t*);
		using CopyRuntimeStateFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, char*, int32_t, int32_t*);
		using SetRuntimeFieldFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, const char*, const char*);
		using CreateInstanceFn = ManagedStatus(__cdecl*)(const char*, ManagedNativeEntity, const char*, uint64_t,
			ManagedScriptInstanceHandle*);
		using SetSerializedFieldsFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, const char*);
		using DestroyInstanceFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle);
		using ConfigureProfilerFn = ManagedStatus(__cdecl*)(const char*, ManagedNativeEntity, uint64_t);
		using InvokeFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle);
		using InvokeCollisionFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, ManagedCollisionEvent);
		using InvokeAnimationEventFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, const char*, float, int32_t,
			const char*);

		using TickFrameFn = ManagedStatus(__cdecl*)(int32_t);
		// Assemblyの解放状態を直接返す接続
		using IntNoArgFn = int32_t(__cdecl*)();

		InitializeNativeAPIFn initializeNativeAPI_ = nullptr;
		ConfigureProfilerFn configureProfiler_ = nullptr;
		LoadGameAssemblyFn loadGameAssembly_ = nullptr;
		UnloadGameAssemblyFn unloadGameAssembly_ = nullptr;
		// SceneのEvent通知
		UnloadGameAssemblyFn pumpSceneEvents_ = nullptr;
		// Applicationの終了通知
		UnloadGameAssemblyFn raiseApplicationQuitting_ = nullptr;
		// phaseごとの予約処理
		TickFrameFn tickFrame_ = nullptr;
		// 直近のAssembly解放結果
		IntNoArgFn getLastALCUnloadStatus_ = nullptr;
		GetScriptTypeCountFn getScriptTypeCount_ = nullptr;
		CopyScriptTypeInfoFn copyScriptTypeInfo_ = nullptr;
		GenerateScriptManifestFn generateScriptManifest_ = nullptr;
		GetScriptSchemaJsonSizeFn getScriptSchemaJsonSize_ = nullptr;
		CopyScriptSchemaJsonFn copyScriptSchemaJson_ = nullptr;
		GetRuntimeStateSizeFn getRuntimeStateSize_ = nullptr;
		CopyRuntimeStateFn copyRuntimeState_ = nullptr;
		GetRuntimeStateSizeFn getSavedStateSize_ = nullptr;
		CopyRuntimeStateFn copySavedState_ = nullptr;
		GetRuntimeStateSizeFn getReloadStateSize_ = nullptr;
		CopyRuntimeStateFn copyReloadState_ = nullptr;
		SetSerializedFieldsFn applyReloadState_ = nullptr;
		SetRuntimeFieldFn setRuntimeField_ = nullptr;
		CreateInstanceFn createInstance_ = nullptr;
		SetSerializedFieldsFn setSerializedFields_ = nullptr;
		UnloadGameAssemblyFn flushPendingReferences_ = nullptr;
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

		// アニメーションイベント呼び出し関数
		InvokeAnimationEventFn invokeAnimationEvent_ = nullptr;

		// 必須exportを順に取得する
		bool Load(DotnetHostResolver& host, const std::filesystem::path& assemblyPath);
	private:
		// 指定したexportを取得する
		template <typename T>
		bool LoadBridgeFunction(DotnetHostResolver& host, const std::filesystem::path& assemblyPath,
			T& outFunction, const wchar_t* methodName);
	};

	template <typename T>
	inline bool ManagedBridgeExports::LoadBridgeFunction(DotnetHostResolver& host, const std::filesystem::path& assemblyPath,
		T& outFunction, const wchar_t* methodName) {

		auto loadAssemblyAndGetFunctionPointer =
			reinterpret_cast<LoadAssemblyAndGetFunctionPointerFn>(host.GetLoadAssemblyDelegate());
		if (!loadAssemblyAndGetFunctionPointer) {
			outFunction = nullptr;
			return false;
		}

		// HostBridgeの公開関数を取得する
		void* function = nullptr;
		const wchar_t* typeName = L"NEMEngine.HostBridge, NEM.ScriptCore";
		const wchar_t* unmanagedCallersOnly = reinterpret_cast<const wchar_t*>(-1);

		int32_t result = loadAssemblyAndGetFunctionPointer(
			assemblyPath.c_str(), typeName, methodName, unmanagedCallersOnly, nullptr, &function);
		if (result != 0 || !function) {
			outFunction = nullptr;
			return false;
		}
		outFunction = reinterpret_cast<T>(function);
		return true;
	}
}
