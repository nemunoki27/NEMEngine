#include "TestContracts.h"
#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestRenderFeatureRuntimeOverrides() {

		Engine::RenderFeatureRuntimeOverrides& overrides =
			Engine::RenderFeatureRuntimeOverrides::GetInstance();
		overrides.ResetAll();
		Engine::MaterialParameterValue value{};
		value.value = 0.75f;
		const Engine::UUID passID{ 101 };
		const Engine::MaterialParameterID parameterID =
			Engine::MaterialParameterID::FromName("ReflectionStrength");
		if (!overrides.SetEnabled(passID, false) ||
			!overrides.SetParameter(passID, parameterID,
				"ReflectionStrength", value)) {

			return false;
		}
		Engine::MaterialParameterValue textureValue{};
		textureValue.value = Engine::AssetID{ 1, 2 };
		const Engine::MaterialParameterID textureID =
			Engine::MaterialParameterID::FromName("gNoiseTexture");
		if (!overrides.SetParameter(passID, textureID,
			"gNoiseTexture", textureValue)) {

			return false;
		}

		const Engine::RenderFeaturePassRuntimeOverride* effect =
			overrides.Find(passID);
		const Engine::MaterialParameterValue* parameter = effect ?
			effect->parameters.Find(parameterID) : nullptr;
		const bool valid = effect && effect->enabled.has_value() &&
			!*effect->enabled && parameter &&
			std::holds_alternative<float>(parameter->value) &&
			std::get<float>(parameter->value) == 0.75f &&
			effect->textureOverrides.contains("gNoiseTexture") &&
			effect->textureOverrides.at("gNoiseTexture") ==
				Engine::AssetID{ 1, 2 };
		const bool cleared = overrides.ClearParameter(
			passID, textureID) &&
			!effect->textureOverrides.contains("gNoiseTexture") &&
			overrides.ClearParameter(passID, parameterID) &&
			overrides.ResetPass(passID) &&
			overrides.SetGroupEnabled("Selective", false) &&
			!overrides.IsGroupEnabled("Selective", true);

		Engine::RenderFeatureProfileAsset profile{};
		Engine::RenderFeaturePassSettings profilePass{};
		profilePass.id = passID;
		profilePass.name = "RuntimeToggle";
		profilePass.material = Engine::AssetID{ 1, 2 };
		profilePass.anchor = Engine::RenderFeatureAnchor::AfterTransparent;
		profilePass.enabled = false;
		profile.passes.emplace_back(profilePass);
		Engine::RenderFeatureProfileRuntime runtime{};
		runtime.Rebuild(profile);
		const bool enabledByScript = overrides.SetEnabled(passID, true) &&
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterTransparent,
				Engine::RenderViewKind::Game).nodes.size() == 1;
		profile.passes.front().enabled = true;
		runtime.Rebuild(profile);
		const bool disabledPassKeepsBypassNode =
			overrides.SetEnabled(passID, false) &&
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterTransparent,
				Engine::RenderViewKind::Game).nodes.size() == 1;
		const bool visibleEnabled = !overrides.IsEnabled(passID, true) &&
			overrides.SetEnabled(passID, true) && overrides.IsEnabled(passID, false);
		overrides.ResetAll();

		// SceneColor出力の切り替えは保存値とパスの有効状態を変更しない
		profile.passes.front().sceneColorOutput = true;
		Engine::RenderFeaturePassSettings second = profile.passes.front();
		second.id = Engine::UUID{ 102 };
		second.name = "RuntimeOutput";
		second.sceneColorOutput = false;
		profile.passes.emplace_back(second);
		Engine::RenderFeaturePassSettings other = second;
		other.id = Engine::UUID{ 103 };
		other.name = "OtherAnchor";
		other.anchor = Engine::RenderFeatureAnchor::BeforeBlit;
		other.sceneColorOutput = true;
		profile.passes.emplace_back(other);
		runtime.Rebuild(profile);
		const bool switched = overrides.SetSceneColorOutput(profile, second.id, true) &&
			!overrides.IsSceneColorOutput(passID, true) &&
			overrides.IsSceneColorOutput(second.id, false) &&
			overrides.IsSceneColorOutput(other.id, true) &&
			overrides.IsEnabled(passID, true) && profile.passes.front().sceneColorOutput &&
			!profile.passes[1].sceneColorOutput &&
			runtime.BuildPlan(second.anchor, Engine::RenderViewKind::Game).sceneColorOutput.pass == second.id &&
			runtime.BuildPlan(second.anchor, Engine::RenderViewKind::Scene).sceneColorOutput.pass == second.id;

		// パラメータの削除後も出力指定を保持する
		const bool retained = overrides.SetParameter(second.id, parameterID, "ReflectionStrength", value) &&
			overrides.ClearParameter(second.id, parameterID) &&
			overrides.IsSceneColorOutput(second.id, false);
		const bool outputOff = overrides.SetSceneColorOutput(profile, second.id, false) &&
			!runtime.BuildPlan(second.anchor, Engine::RenderViewKind::Game).sceneColorOutput.pass &&
			runtime.BuildPlan(second.anchor, Engine::RenderViewKind::Game).nodes.size() == 2;

		// 出力設定が不正な要求は現在の指定を維持して拒否する
		overrides.SetSceneColorOutput(profile, second.id, true);
		profile.passes.front().outputs.emplace_back(Engine::RenderFeatureOutputSettings{});
		profile.passes.front().outputs.front().widthScale = 0.5f;
		const bool rejected = !overrides.SetSceneColorOutput(profile, passID, true) &&
			!overrides.SetSceneColorOutput(profile, Engine::UUID{ 999 }, true) &&
			overrides.IsSceneColorOutput(second.id, false) &&
			!overrides.IsSceneColorOutput(passID, true);
		const bool resetPass = overrides.ResetPass(second.id) &&
			!overrides.IsSceneColorOutput(second.id, false);
		overrides.ResetAll();
		return valid && cleared && enabledByScript &&
			disabledPassKeepsBypassNode && visibleEnabled && switched && retained &&
			outputOff && rejected && resetPass && overrides.IsSceneColorOutput(passID, true) &&
			overrides.Find(passID) == nullptr;
	}

	bool TestShaderPathDependencies() {

		const std::filesystem::path testRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() /
			"Tests" / "RenderFeatureDependencies";
		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		const std::filesystem::path sourcePath = testRoot / "reload.CS.hlsl";
		{
			std::ofstream source(sourcePath, std::ios::binary);
			source << "[numthreads(1, 1, 1)] void main() {}";
		}
		const std::filesystem::path shaderPath = testRoot / "reload.shader.json";
		const nlohmann::json shader = {
			{ "name", "ReloadTest" },
			{ "sourceShader",
				"GameAssets/Tests/RenderFeatureDependencies/reload.CS.hlsl" },
			{ "stages", nlohmann::json::array({ {
				{ "stage", "CS" },
				{ "file",
					"GameAssets/Tests/RenderFeatureDependencies/reload.CS.hlsl" },
				{ "entry", "main" },
				{ "profile", "cs_6_0" },
			} }) },
		};
		if (!Engine::JsonAdapter::SaveCanonical(shaderPath, shader)) {
			std::filesystem::remove_all(testRoot, ec);
			return false;
		}

		Engine::AssetDatabase database{};
		database.Init();
		const Engine::AssetID sourceID = database.ImportOrGet(
			"GameAssets/Tests/RenderFeatureDependencies/reload.CS.hlsl",
			Engine::AssetType::Shader);
		const Engine::AssetID shaderID = database.ImportOrGet(
			"GameAssets/Tests/RenderFeatureDependencies/reload.shader.json",
			Engine::AssetType::Shader);
		database.RefreshDependencies(shaderID);
		const std::vector<Engine::AssetID>& dependencies =
			database.FindDependencies(shaderID);
		const std::vector<Engine::AssetID>& referencers =
			database.FindReferencers(sourceID);
		bool passed = sourceID && shaderID &&
			std::find(dependencies.begin(), dependencies.end(), sourceID) !=
				dependencies.end() &&
			std::find(referencers.begin(), referencers.end(), shaderID) !=
					referencers.end();
		// 派生IDは元グラフへ依存し、独自の参照切れは隠さない
		const auto graph = Engine::CreateDefaultSurfaceShaderGraph("Dependencies");
		passed &= Engine::JsonAdapter::SaveCanonical(testRoot / "test.shadergraph.json", Engine::ToJson(graph));
		const auto graphID = database.ImportOrGet(
			"GameAssets/Tests/RenderFeatureDependencies/test.shadergraph.json", Engine::AssetType::ShaderGraph);
		auto material = Engine::ShaderGraphArtifactCache::CreateMaterial(graph, graphID);
		const auto artifact = Engine::ShaderGraphArtifactCache::DescribeReferences(graph, graphID);
		Engine::ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
		const Engine::AssetID missingID{ 123, 456 };
		Engine::FindPass(material, Engine::MaterialPassKind::Transparent)->pipeline = missingID;
		passed &= Engine::JsonAdapter::SaveCanonical(testRoot / "test.material.json", Engine::ToJson(material));
		const auto materialID = database.ImportOrGet(
			"GameAssets/Tests/RenderFeatureDependencies/test.material.json", Engine::AssetType::Material);
		database.RefreshDependencies(materialID);
		const auto& graphDependencies = database.FindDependencies(materialID);
		passed &= std::find(graphDependencies.begin(), graphDependencies.end(), graphID) != graphDependencies.end() &&
			std::find(graphDependencies.begin(), graphDependencies.end(), missingID) != graphDependencies.end() &&
			std::find(graphDependencies.begin(), graphDependencies.end(), artifact.opaquePipelineID) == graphDependencies.end() &&
			std::find(graphDependencies.begin(), graphDependencies.end(), artifact.opaqueShaderID) == graphDependencies.end();
		passed &= std::any_of(database.GetIssues().begin(), database.GetIssues().end(), [&](const auto& issue) {
			return issue.assetID == materialID && issue.referencedAssetID == missingID &&
				issue.type == Engine::AssetDatabaseIssueType::MissingReference;
		});
		passed &= std::none_of(database.GetIssues().begin(), database.GetIssues().end(), [&](const auto& issue) {
			return issue.assetID == materialID && issue.referencedAssetID != missingID;
		});
		// 元グラフが欠損していれば派生参照も再生成できない
		material.shaderGraph = Engine::AssetID{ 123, 789 };
		passed &= Engine::JsonAdapter::SaveCanonical(testRoot / "test.material.json", Engine::ToJson(material));
		database.RefreshDependencies(materialID);
		const auto& missingDependencies = database.FindDependencies(materialID);
		passed &= std::find(missingDependencies.begin(), missingDependencies.end(), artifact.opaqueShaderID) !=
			missingDependencies.end();
		std::filesystem::remove_all(testRoot, ec);
		return passed && !ec;
	}
}
