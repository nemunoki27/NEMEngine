#include "AntialiasingTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/GraphicsPreferenceStorage.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureAntialiasingMigration.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderPassesAsset.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/CameraSurfacePipeline.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RenderFeatureResourceUtility.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessDebugInjector.h>
#include <Engine/Core/Rendering/Renderer/Views/CameraViewProjection.h>

// c++
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace {

	// 未設定時と保存済み設定を読み込み、削除された方式を引き継がない
	bool CheckSettings() {

		using namespace Engine;
		GraphicsFeaturePreferences preferences{};
		if (preferences.antialiasing != AntialiasingMethod::None) return false;
		for (auto method : {AntialiasingMethod::None, AntialiasingMethod::FXAA}) {
			preferences.antialiasing = method;
			preferences.fxaa = {0.75f, 0.25f, 0.125f, 0.5f};
			preferences.fxaaSettingsMigrated = true;
			GraphicsFeaturePreferences restored{};
			GraphicsPreferenceStorage::Read(GraphicsPreferenceStorage::Write(preferences), restored);
			if (restored.antialiasing != method || restored.fxaa != preferences.fxaa || !restored.fxaaSettingsMigrated) return false;
		}
		for (const char* removed : {"DLSSSuperResolution", "DLAA"}) {
			nlohmann::json legacy = {{"antialiasing", removed}, {"dlssQuality", "Quality"},
				{"rayReconstruction", true}, {"frameContextCount", 2}, {"fxaaStrength", 0.75f}};
			if (!GraphicsPreferenceStorage::Migrate(legacy) || legacy.at("antialiasing") != "None" ||
				legacy.contains("dlssQuality") || legacy.contains("rayReconstruction") ||
				legacy.at("frameContextCount") != 2 || legacy.at("fxaaStrength") != 0.75f ||
				GraphicsPreferenceStorage::Migrate(legacy)) return false;
			GraphicsPreferenceStorage::Read(legacy, preferences);
			const auto saved = GraphicsPreferenceStorage::Write(preferences);
			if (preferences.antialiasing != AntialiasingMethod::None || saved.contains("dlssQuality") ||
				saved.contains("rayReconstruction")) return false;
		}
		GraphicsPreferenceStorage::Read({{"fxaaStrength", -2.0f}, {"fxaaEdgeThreshold", 10.0f},
			{"fxaaMinimumThreshold", std::numeric_limits<float>::infinity()}}, preferences);
		return preferences.fxaa.strength == 0.0f && preferences.fxaa.edgeThreshold == 1.0f &&
			preferences.fxaa.minimumThreshold == 0.125f;
	}

	// 表示名に依存せず既定マテリアルだけを移行する
	bool CheckMigration() {

		using namespace Engine;
		RenderFeatureProfileAsset profile{};
		RenderFeaturePassSettings custom{};
		custom.id = Engine::UUID{1};
		custom.name = "FXAA";
		custom.material = AssetID{1, 1};
		custom.outputs.emplace_back();
		RenderFeaturePassSettings legacy = custom;
		legacy.id = Engine::UUID{2};
		legacy.name = "旧アンチエイリアス";
		legacy.material = BuiltinAssets::Materials::FXAA;
		legacy.parameterOverrides.Set("edgeThreshold", MaterialParameterValue{0.375f});
		RenderFeaturePassSettings destination = custom;
		destination.id = Engine::UUID{3};
		destination.name = "後段";
		destination.sourceKind = RenderFeatureSourceKind::PassOutput;
		destination.source = {legacy.id, "Color"};
		destination.passInputs["gPreviousColor"] = {legacy.id, "Color"};
		profile.passes = {custom, legacy, destination};
		const auto restored = RenderFeatureProfileSerializer::FromJson(RenderFeatureProfileSerializer::ToJson(profile));
		if (restored.passes.size() != 2 || restored.passes.front().material != custom.material ||
			!restored.legacyFXAASettings || restored.legacyFXAASettings->edgeThreshold != 0.375f ||
			restored.passes.back().source.pass != custom.id ||
			restored.passes.back().passInputs.at("gPreviousColor").pass != custom.id) return false;
		RenderFeatureProfileRuntime runtime;
		runtime.Rebuild(restored);
		if (!runtime.BuildPlan(RenderFeatureAnchor::AfterTransparent, RenderViewKind::Game).IsValid()) return false;
		RenderPassesAsset asset{};
		if (!FromJson(RenderFeatureProfileSerializer::ToJson(profile), asset) || !ToRuntimeProfile(asset).legacyFXAASettings) return false;
		MigrateRenderFeatureAntialiasing(profile);
		return profile.passes.size() == 2 && profile.legacyFXAASettings &&
			std::ranges::find(BuiltinAssets::Runtime::Assets, BuiltinAssets::Materials::FXAA) !=
				std::end(BuiltinAssets::Runtime::Assets);
	}

	// シーンカラーへの参照と空になった選択Groupを保存後も維持する
	bool CheckSceneColorReference() {

		using namespace Engine;
		RenderFeatureProfileAsset profile{};
		RenderFeaturePassSettings legacy{};
		legacy.id = Engine::UUID{10};
		legacy.name = "旧FXAA";
		legacy.material = BuiltinAssets::Materials::FXAA;
		legacy.outputs.emplace_back();
		RenderFeaturePassSettings after{};
		after.id = Engine::UUID{11};
		after.name = "後段";
		after.material = AssetID{1, 2};
		after.sourceKind = RenderFeatureSourceKind::PassOutput;
		after.source = {legacy.id, "Color"};
		after.passInputs["gOtherColor"] = {legacy.id, "Color"};
		after.outputs.emplace_back();
		profile.passes = {legacy, after};
		RenderFeatureHierarchyItem group{};
		group.id = Engine::UUID{20};
		group.type = RenderFeatureHierarchyItemType::Group;
		group.selection.mode = RenderFeatureSelectionMode::MaskedSceneColor;
		group.children.push_back({.id = legacy.id});
		profile.hierarchy = {group, {.id = after.id}};
		MigrateRenderFeatureAntialiasing(profile);
		const auto restored = RenderFeatureProfileSerializer::FromJson(RenderFeatureProfileSerializer::ToJson(profile));
		if (restored.passes.size() != 1 || restored.hierarchy.size() != 1 ||
			restored.hierarchy.front().type != RenderFeatureHierarchyItemType::Pass ||
			restored.passes.front().sourceKind != RenderFeatureSourceKind::SceneColor ||
			!restored.passes.front().passInputs.contains("gOtherColor") ||
			restored.passes.front().passInputs.at("gOtherColor").pass) return false;
		RenderFeatureProfileRuntime runtime;
		runtime.Rebuild(restored);
		return runtime.BuildPlan(RenderFeatureAnchor::AfterTransparent, RenderViewKind::Game).IsValid();
	}

	// 固定の遮蔽と反射は既存のフィルターを通す
	bool CheckSurfaceProfiles() {

		using namespace Engine;
		CameraCommon settings{};
		for (int32_t quality = 0; quality <= 2; ++quality) {
			settings.reflectionQuality = settings.ambientOcclusionQuality = quality;
			const auto ao = BuildCameraAmbientOcclusionProfile(settings);
			if (ao.passes.size() != 2 || ao.passes.back().id != CameraSurfacePassIDs::AmbientOcclusionFilter ||
				!ao.passes.back().outputs.front().history || BuildCameraReflectionProfile(settings).passes.size() != 7) return false;
			RenderFeatureProfileRuntime runtime;
			runtime.Rebuild(ao);
			if (!runtime.BuildPlan(RenderFeatureAnchor::BeforeLighting, RenderViewKind::Game).IsValid()) return false;
		}
		return true;
	}

	// 後段の参照がないときは中間カラーのコピーを要求しない
	bool CheckSceneInputRequirements() {

		using namespace Engine;
		SceneExecutionContext context{};
		RenderPassPhaseBuckets buckets{};
		RenderPipelineDeps deps{};
		const auto required = [&] { return RenderFeatureResourceUtility::RequiresSceneColorSnapshot(context, buckets, deps); };
		if (required()) return false;
		RenderFeatureProfileAsset profile{};
		RenderFeaturePassSettings pass{};
		pass.id = Engine::UUID{31};
		pass.material = AssetID{1, 2};
		pass.anchor = RenderFeatureAnchor::BeforeLighting;
		pass.outputs.emplace_back();
		profile.passes.push_back(pass);
		RenderFeatureProfileRuntime runtime;
		context.renderPassesRuntime = &runtime;
		runtime.Rebuild(profile);
		if (required()) return false;
		profile.passes.front().anchor = RenderFeatureAnchor::AfterTransparent;
		runtime.Rebuild(profile);
		if (!required()) return false;
		profile.passes.front().enabled = false;
		runtime.Rebuild(profile);
		if (required()) return false;
		RenderItem item{};
		buckets.Get(RenderPhase::PostProcessUI).items.push_back(&item);
		if (!required()) return false;
		buckets.Clear();
		PostProcessDebugInjector injector;
		deps.postProcessDebugInjector = &injector;
		injector.SetEnabled(true);
		if (!required()) return false;
		injector.SetEnabled(false);
		return !required();
	}
}

bool NEMTests::TestAntialiasingSettings() {

	return CheckSettings() && CheckMigration() && CheckSceneColorReference() && CheckSurfaceProfiles() && CheckSceneInputRequirements();
}
