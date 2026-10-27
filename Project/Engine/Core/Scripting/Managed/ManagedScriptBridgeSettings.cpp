#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Runtime/Settings/RuntimeProjectSettings.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <fstream>
#include <limits>

namespace {

	// 呼出し元のApplicationが所有する設定を解決する
	Engine::RuntimeProjectSettings* ResolveSettings() {

		const auto* context = Engine::ManagedScriptRuntime::GetCurrentContext();
		return context && context->engineContext ? context->engineContext->GetRuntimeSettings() : nullptr;
	}
}

//============================================================================
//	ManagedScriptRuntime settingsMethods
//============================================================================
int32_t Engine::ManagedScriptRuntime::CopyRuntimeSettingsCallback(int32_t effective, char* buffer, int32_t capacity) {

	const auto* settings = ResolveSettings();
	if (!settings) return -1;
	try {
		return CopyStringToBuffer((effective ? settings->ReadEffective() : settings->Read()).dump(), buffer, capacity);
	} catch (...) { return -1; }
}

int32_t Engine::ManagedScriptRuntime::ChangeRuntimeSettingsCallback(int32_t action, const char* buffer, int32_t size) {

	auto* settings = ResolveSettings();
	if (!settings) return static_cast<int32_t>(ManagedStatus::InvalidWorldHandle);
	try {
		// 保存と再読込は明示要求された場合だけ行う
		if (action == 1) return static_cast<int32_t>(settings->SaveUserSettings() ? ManagedStatus::Ok : ManagedStatus::InternalError);
		if (action == 2) return static_cast<int32_t>(settings->LoadUserSettings() ? ManagedStatus::Ok : ManagedStatus::InvalidArgument);
		if (action == 3) { settings->ResetToProjectDefaults(); return static_cast<int32_t>(ManagedStatus::Ok); }
		if (action != 0 || !buffer || size <= 0 || size > 1024 * 1024) return static_cast<int32_t>(ManagedStatus::InvalidArgument);
		return static_cast<int32_t>(settings->Request(nlohmann::json::parse(buffer, buffer + size)) ?
			ManagedStatus::Ok : ManagedStatus::InvalidArgument);
	} catch (...) { return static_cast<int32_t>(ManagedStatus::SerializationError); }
}

int32_t Engine::ManagedScriptRuntime::CopyTextAssetBytesCallback(ManagedAssetGUID assetID, uint8_t* buffer, int32_t capacity) {

	const auto* context = GetCurrentContext();
	if (!context || !context->assetDatabase || capacity < 0) return -1;
	const auto* meta = context->assetDatabase->Find(ToAssetID(assetID));
	if (!meta || meta->type != AssetType::Text) return -1;
	try {
		// 容量確認だけならファイル本体を読み込まない
		std::ifstream stream(context->assetDatabase->ResolveFullPath(meta->guid), std::ios::binary | std::ios::ate);
		const auto length = stream.tellg();
		if (!stream || length < 0 || length > 64 * 1024 * 1024) return -1;
		const auto size = static_cast<int32_t>(length);
		if (!buffer || capacity < size) return size;
		stream.seekg(0);
		if (size && !stream.read(reinterpret_cast<char*>(buffer), size)) return -1;
		return size;
	} catch (...) { return -1; }
}
