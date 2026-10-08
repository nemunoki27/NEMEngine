#include "RenderFeatureProfileDocument.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfileSerializer.h"
#include "RenderPassesAsset.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <type_traits>
#include <utility>

//============================================================================
//	RenderFeatureProfileDocument classMethods
//============================================================================

bool Engine::RenderFeatureProfileDocument::Read(const std::filesystem::path& path) {

	// 読込失敗時は現在の編集を保持する
	std::filesystem::path candidatePath = path;
	RenderFeatureProfileAsset candidate{};
	if (!candidatePath.empty() && !RenderFeatureProfileSerializer::Load(candidatePath, candidate)) {

		Logger::Output(LogType::Engine, spdlog::level::err, "[レンダー機能] プロファイルの読み込みに失敗しました path={}",
			candidatePath.string());
		return false;
	}
	static_assert(std::is_nothrow_move_assignable_v<std::filesystem::path>);
	static_assert(std::is_nothrow_move_assignable_v<RenderFeatureProfileAsset>);
	profilePath_ = std::move(candidatePath);
	profile_ = std::move(candidate);
	return true;
}

bool Engine::RenderFeatureProfileDocument::Save() const {

	if (profilePath_.empty()) {
		return false;
	}
	if (profilePath_.filename().string().ends_with(".renderpasses.json")) {
		RenderPassesAsset extension{};
		extension.guid = profile_.guid;
		extension.name = profile_.name;
		extension.passes = profile_.passes;
		extension.hierarchy = profile_.hierarchy;
		return JsonAdapter::SaveCanonical(profilePath_, ToJson(extension));
	}
	return false;
}
