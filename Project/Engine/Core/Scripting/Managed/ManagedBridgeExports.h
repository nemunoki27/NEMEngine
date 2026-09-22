#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Scripting/Managed/DotnetHostResolver.h>

namespace Engine {

	//============================================================================
	//	ManagedBridgeExports structure
	//	接続済みManaged関数と取得処理
	//============================================================================
	struct ManagedBridgeExports {

		// load_assembly_and_get_function_pointerデリゲートのシグネチャ、x64では__stdcallと__cdeclが同一ABIで呼び出し可能
		using LoadAssemblyAndGetFunctionPointerFn = int32_t(__cdecl*)(const wchar_t*, const wchar_t*,
			const wchar_t*, const wchar_t*, void*, void**);

		// 全exportは例外を境界外へ出さずManagedStatusで返し、値を返すAPIはout parameter形式にする
		using InitializeNativeAPIFn = ManagedStatus(__cdecl*)(ManagedNativeAPITable*);
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
		using ConfigureProfilerFn = ManagedStatus(__cdecl*)(const char*, ManagedNativeEntity, uint64_t);
		using InvokeFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle);
		using InvokeCollisionFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, ManagedCollisionEvent);
		using InvokeAnimationEventFn = ManagedStatus(__cdecl*)(ManagedScriptInstanceHandle, const char*, float, int32_t, const char*);

		using TickFrameFn = ManagedStatus(__cdecl*)(int32_t);
		using IntNoArgFn = int32_t(__cdecl*)();

		InitializeNativeAPIFn initializeNativeAPI_ = nullptr;
		ConfigureProfilerFn configureProfiler_ = nullptr;
		LoadGameAssemblyFn loadGameAssembly_ = nullptr;
		UnloadGameAssemblyFn unloadGameAssembly_ = nullptr;
		// SceneイベントpumpのPumpSceneEventsでUnloadGameAssemblyFnと同じ無引数シグネチャ
		UnloadGameAssemblyFn pumpSceneEvents_ = nullptr;
		// application終了通知のRaiseApplicationQuittingで同じ無引数シグネチャ
		UnloadGameAssemblyFn raiseApplicationQuitting_ = nullptr;
		// per-frame tickのTickFrameでphaseを引数に取る
		TickFrameFn tickFrame_ = nullptr;
		// 直近ALC unload statusのGetLastAlcUnloadStatusでintを返す無引数
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
