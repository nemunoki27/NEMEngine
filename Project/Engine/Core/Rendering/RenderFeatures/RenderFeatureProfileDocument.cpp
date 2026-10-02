#include "RenderFeatureProfileDocument.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfileSerializer.h"
#include "RenderExtensionAsset.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

void Engine::RenderFeatureProfileDocument::Read() {

	profile_ = RenderFeatureProfileAsset{};
	if (!profilePath_.empty() &&
		!RenderFeatureProfileSerializer::Load(profilePath_, profile_)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レンダー機能] プロファイルの読み込みに失敗しました path={}",
			profilePath_.string());
	}
}

bool Engine::RenderFeatureProfileDocument::Save() const {

	if (profilePath_.empty()) {
		return false;
	}
	if (profilePath_.filename().string().ends_with(".renderextension.json")) {
		RenderExtensionAsset extension{};
		extension.guid = profile_.guid;
		extension.name = profile_.name;
		extension.passes = profile_.passes;
		extension.hierarchy = profile_.hierarchy;
		JsonAdapter::Save(profilePath_, ToJson(extension));
		return true;
	}
	return RenderFeatureProfileSerializer::Save(profilePath_, profile_);
}
