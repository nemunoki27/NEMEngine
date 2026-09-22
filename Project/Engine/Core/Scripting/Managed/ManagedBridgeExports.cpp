#include "ManagedBridgeExports.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include "ManagedBuildUtility.h"

using Engine::ManagedBuildUtility::ToUtf8Path;

bool Engine::ManagedBridgeExports::Load(DotnetHostResolver& host, const std::filesystem::path& assemblyPath) {

	auto loadRequired = [this, &host, &assemblyPath](auto& function, const wchar_t* methodName) {

		if (LoadBridgeFunction(host, assemblyPath, function, methodName)) {
			return true;
		}
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: 必須Bridge関数が見つかりません method={} assembly={}",
			Algorithm::ConvertString(methodName), ToUtf8Path(assemblyPath));
		return false;
	};

	bool success = true;
	success &= loadRequired(initializeNativeAPI_, L"InitializeNativeAPI");
	success &= loadRequired(loadGameAssembly_, L"LoadGameAssembly");
	success &= loadRequired(unloadGameAssembly_, L"UnloadGameAssembly");
	success &= loadRequired(pumpSceneEvents_, L"PumpSceneEvents");
	success &= loadRequired(raiseApplicationQuitting_, L"RaiseApplicationQuitting");
	success &= loadRequired(tickFrame_, L"TickFrame");
	success &= loadRequired(configureProfiler_, L"ConfigureScriptProfiler");
	success &= loadRequired(getLastAlcUnloadStatus_, L"GetLastAlcUnloadStatus");
	success &= loadRequired(getScriptTypeCount_, L"GetScriptTypeCount");
	success &= loadRequired(copyScriptTypeInfo_, L"CopyScriptTypeInfo");
	success &= loadRequired(generateScriptManifest_, L"GenerateScriptManifest");
	success &= loadRequired(getScriptSchemaJsonSize_, L"GetScriptSchemaJsonSize");
	success &= loadRequired(copyScriptSchemaJson_, L"CopyScriptSchemaJson");
	success &= loadRequired(getRuntimeStateSize_, L"GetRuntimeSerializedStateSize");
	success &= loadRequired(copyRuntimeState_, L"CopyRuntimeSerializedState");
	success &= loadRequired(setRuntimeField_, L"SetRuntimeSerializedField");
	success &= loadRequired(createInstance_, L"CreateInstance");
	success &= loadRequired(setSerializedFields_, L"SetSerializedFields");
	success &= loadRequired(flushPendingReferences_, L"FlushPendingReferences");
	success &= loadRequired(destroyInstance_, L"DestroyInstance");
	success &= loadRequired(invokeAwake_, L"InvokeAwake");
	success &= loadRequired(invokeStart_, L"InvokeStart");
	success &= loadRequired(invokeOnEnable_, L"InvokeOnEnable");
	success &= loadRequired(invokeOnDisable_, L"InvokeOnDisable");
	success &= loadRequired(invokeOnDestroy_, L"InvokeOnDestroy");
	success &= loadRequired(invokeFixedUpdate_, L"InvokeFixedUpdate");
	success &= loadRequired(invokeUpdate_, L"InvokeUpdate");
	success &= loadRequired(invokeLateUpdate_, L"InvokeLateUpdate");
	success &= loadRequired(invokeCollisionEnter_, L"InvokeCollisionEnter");
	success &= loadRequired(invokeCollisionStay_, L"InvokeCollisionStay");
	success &= loadRequired(invokeCollisionExit_, L"InvokeCollisionExit");
	success &= loadRequired(invokeAnimationEvent_, L"InvokeAnimationEvent");
	return success;
}
