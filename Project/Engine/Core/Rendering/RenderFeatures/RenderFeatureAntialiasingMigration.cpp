#include "RenderFeatureAntialiasingMigration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace {

	// 旧マテリアルの既定値へProfileの上書き値を重ねる
	Engine::FXAASettings ReadLegacySettings(const Engine::MaterialParameterSet& parameters) {

		Engine::FXAASettings settings{};
		settings.edgeThreshold = 0.125f;
		const auto read = [&](std::string_view name, float& destination) {
			if (const auto* parameter = parameters.FindByName(name)) {
				if (const float* value = std::get_if<float>(&parameter->value); value && std::isfinite(*value)) {
					destination = std::clamp(*value, 0.0f, 1.0f);
				}
			}
		};
		read("strength", settings.strength);
		read("edgeThreshold", settings.edgeThreshold);
		read("minimumThreshold", settings.minimumThreshold);
		read("subpixelStrength", settings.subpixelStrength);
		return settings;
	}

	// FXAAだけが残っていたGroupも階層から取り除く
	void PruneHierarchy(std::vector<Engine::RenderFeatureHierarchyItem>& items,
		const std::unordered_set<uint64_t>& removed) {

		std::erase_if(items, [&](auto& item) {
			if (item.type == Engine::RenderFeatureHierarchyItemType::Pass) return removed.contains(item.id.value);
			const bool hadChildren = !item.children.empty();
			PruneHierarchy(item.children, removed);
			return hadChildren && item.children.empty();
		});
	}
}

//============================================================================
//	RenderFeatureAntialiasingMigration functions
//============================================================================

void Engine::MigrateRenderFeatureAntialiasing(RenderFeatureProfileAsset& profile) {

	std::unordered_set<uint64_t> removed{};
	for (const auto& pass : profile.passes) {
		if (pass.material != BuiltinAssets::Materials::FXAA) continue;
		removed.emplace(pass.id.value);
		if (!profile.legacyFXAASettings) profile.legacyFXAASettings = ReadLegacySettings(pass.parameterOverrides);
	}
	if (removed.empty()) return;

	// 階層の有効状態を使い、各FXAAが読んでいたカラーを記録する
	RenderFeatureProfileAsset ordered = profile;
	ApplyRenderFeatureHierarchy(ordered);
	std::array<RenderFeatureOutputReference, static_cast<size_t>(RenderFeatureAnchor::AfterToneMap) + 1> previous{};
	std::unordered_map<uint64_t, RenderFeatureOutputReference> replacements{};
	for (const auto& pass : ordered.passes) {
		auto& preceding = previous[static_cast<size_t>(pass.anchor)];
		if (removed.contains(pass.id.value)) {
			replacements[pass.id.value] = pass.sourceKind == RenderFeatureSourceKind::PassOutput ? pass.source :
				pass.sourceKind == RenderFeatureSourceKind::PreviousPass ? preceding : RenderFeatureOutputReference{};
		}
		if (pass.enabled) preceding = {pass.id, pass.outputs.empty() ? "Color" : pass.outputs.front().name};
	}
	const auto reconnect = [&](RenderFeatureOutputReference& reference) {
		for (size_t remaining = removed.size(); reference.pass && removed.contains(reference.pass.value);) {
			if (remaining-- == 0) {
				reference = {};
				break;
			}
			reference = replacements.at(reference.pass.value);
		}
	};
	std::erase_if(profile.passes, [&](const auto& pass) { return removed.contains(pass.id.value); });
	for (auto& pass : profile.passes) {
		if (pass.sourceKind == RenderFeatureSourceKind::PassOutput && removed.contains(pass.source.pass.value)) {
			reconnect(pass.source);
			if (!pass.source.pass) pass.sourceKind = RenderFeatureSourceKind::SceneColor;
		}
		for (auto& [name, reference] : pass.passInputs) reconnect(reference);
	}
	PruneHierarchy(profile.hierarchy, removed);
	NormalizeRenderFeatureHierarchy(profile);
	SynchronizeRenderFeaturePassOrder(profile);
}
