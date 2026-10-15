#include "BloomTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/Bloom/BloomSettings.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

// c++
#include <cmath>
#include <limits>

bool NEMTests::TestBloomSettings() {

	using namespace Engine;
	// 標準Assetのmetaを本番と同じ読込処理で検証する
	for (const auto* stage : {"Prefilter", "Downsample", "Upsample", "Composite"}) {
		for (const auto* kind : {"shader", "material", "pipeline"}) {
			AssetMeta meta;
			const auto path = RuntimePaths::GetEngineAssetPath("Shaders/Builtin/PostProcess/Bloom/bloom" + std::string(stage) + "." + kind + ".json.meta");
			if (!AssetMetaStorage::ReadMetaFile(path, meta)) return false;
		}
	}
	RenderFeatureProfileAsset profile;
	RenderFeaturePassSettings pass;
	pass.id = Engine::UUID::New();
	pass.name = "Bloom";
	pass.type = RenderFeaturePassType::Bloom;
	WriteBloomSettings(pass.parameterOverrides, BloomSettings{});
	profile.passes.push_back(pass);
	// 同じPass種別とパラメータ名で往復する
	const auto loaded = RenderFeatureProfileSerializer::FromJson(RenderFeatureProfileSerializer::ToJson(profile));
	if (loaded.passes.size() != 1 || loaded.passes[0].type != RenderFeaturePassType::Bloom) return false;
	const auto settings = ReadBloomSettings(loaded.passes[0].parameterOverrides);
	if (settings.intensity != 0.6f || settings.scatter != 0.7f || settings.threshold != 1.0f) return false;
	RenderFeatureProfileRuntime runtime;
	runtime.Rebuild(loaded);
	const auto plan = runtime.GetPlan(RenderFeatureAnchor::AfterTransparent, RenderViewKind::Game);
	if (!plan->IsValid() || plan->nodes.size() != 1 || plan->sceneColorOutput.pass != pass.id) return false;

	// C#と同じ上書き窓口を使い、解除後は保存値へ戻す
	auto& overrides = RenderFeatureRuntimeOverrides::GetInstance();
	overrides.SetParameter(pass.id, MaterialParameterID::FromName("intensity"), "intensity", {3.0f});
	const auto* value = overrides.Find(pass.id);
	if (!value || ReadBloomSettings(pass.parameterOverrides, &value->parameters).intensity != 3.0f) return false;
	overrides.ResetPass(pass.id);
	if (overrides.Find(pass.id) || ReadBloomSettings(pass.parameterOverrides).intensity != 0.6f) return false;
	// 無効時は主入力を引き継ぎ、解除後は同じ出力へ戻す
	overrides.SetEnabled(pass.id, false);
	const auto disabled = runtime.GetPlan(RenderFeatureAnchor::AfterTransparent, RenderViewKind::Game);
	if (!disabled->IsValid() || disabled->sceneColorOutput.pass) return false;
	overrides.ResetPass(pass.id);
	if (runtime.GetPlan(RenderFeatureAnchor::AfterTransparent, RenderViewKind::Game)->nodes.size() != 1) return false;
	MaterialParameterSet integers;
	integers.Set("threshold", {int32_t{3}});
	if (ReadBloomSettings(integers).threshold != 3.0f) return false;
	MaterialParameterSet invalid;
	invalid.Set("intensity", {std::numeric_limits<float>::quiet_NaN()});
	invalid.Set("scatter", {8.0f});
	invalid.Set("threshold", {-1.0f});
	const auto safe = ReadBloomSettings(invalid);
	if (!std::isfinite(safe.intensity) || safe.scatter != 1.0f || safe.threshold != 0.0f) return false;

	// 小さい画像と奇数解像度でも0寸法を作らない
	const auto portrait = BuildBloomLevelSizes(65, 129);
	const auto tiny = BuildBloomLevelSizes(1, 1);
	if (portrait.size() != 5 || portrait[0].width != 32 || portrait[4].height != 4 || tiny.size() != 1) return false;
	if (!BuildBloomLevelSizes(0, 64).empty()) return false;
	profile.passes[0].anchor = RenderFeatureAnchor::AfterToneMap;
	runtime.Rebuild(profile);
	if (runtime.GetDiagnostic().empty()) return false;
	profile.passes[0].anchor = RenderFeatureAnchor::AfterTransparent;
	profile.passes[0].outputs.push_back({.widthScale = 0.5f});
	runtime.Rebuild(profile);
	return !runtime.GetDiagnostic().empty();
}
