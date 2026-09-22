#include "RenderFeatureProfileDocument.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfileSerializer.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>

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
	return RenderFeatureProfileSerializer::Save(profilePath_, profile_);
}
