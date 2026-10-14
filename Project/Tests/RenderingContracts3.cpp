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
#include <Engine/Core/Rendering/DebugDraw/Lines/SceneGridLayout.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
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

	bool TestSceneGridProjection() {

		Engine::ResolvedCameraView camera{};
		camera.valid = true;
		camera.cameraPos = Engine::Vector3(7.0f, 10.0f, -3.0f);
		camera.matrices.inverseViewMatrix = Engine::Matrix4x4::MakeAffineMatrix(
			Engine::Vector3::AnyInit(1.0f), Engine::Vector3(90.0f, 0.0f, 0.0f), camera.cameraPos);
		camera.matrices.viewMatrix = Engine::Matrix4x4::Inverse(camera.matrices.inverseViewMatrix);
		std::vector<Engine::SceneGridLayout::GridPoint2D> polygon;

		// 真上の平行投影でも画面幅と奥行きを保つ
		camera.projectionMode = Engine::ResolvedProjectionMode::Orthographic;
		camera.matrices.projectionMatrix = Engine::Matrix4x4::MakeOrthographicMatrix(-4.0f, 2.0f, 4.0f, -2.0f, 0.1f, 100.0f);
		camera.matrices.inverseProjectionMatrix = Engine::Matrix4x4::Inverse(camera.matrices.projectionMatrix);
		camera.matrices.viewProjectionMatrix = camera.matrices.viewMatrix * camera.matrices.projectionMatrix;
		if (!Engine::SceneGridLayout::BuildVisibleGroundPolygon(camera, 4, 0.0f, 1000.0f, polygon)) {
			return false;
		}
		for (const auto& point : polygon) {
			if (point.x < 2.99f || 11.01f < point.x || point.z < -5.01f || -0.99f < point.z) {
				return false;
			}
		}
		const auto [minX, maxX] =
			std::minmax_element(polygon.begin(), polygon.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
		const auto [minZ, maxZ] =
			std::minmax_element(polygon.begin(), polygon.end(), [](const auto& a, const auto& b) { return a.z < b.z; });
		if (std::abs(maxX->x - minX->x - 8.0f) > 0.001f || std::abs(maxZ->z - minZ->z - 4.0f) > 0.001f) {
			return false;
		}

		// 視線が平面に平行な場合は範囲を作らない
		const auto inverseView = camera.matrices.inverseViewMatrix;
		camera.matrices.inverseViewMatrix = Engine::Matrix4x4::MakeTranslateMatrix(camera.cameraPos);
		if (Engine::SceneGridLayout::BuildVisibleGroundPolygon(camera, 4, 0.0f, 1000.0f, polygon) || !polygon.empty()) {
			return false;
		}

		// 透視投影も視野角に合う平面範囲を保つ
		camera.projectionMode = Engine::ResolvedProjectionMode::Perspective;
		camera.matrices.inverseViewMatrix = inverseView;
		camera.matrices.projectionMatrix = Engine::Matrix4x4::MakePerspectiveFovMatrix(90.0f, 2.0f, 0.1f, 100.0f);
		camera.matrices.inverseProjectionMatrix = Engine::Matrix4x4::Inverse(camera.matrices.projectionMatrix);
		camera.matrices.viewProjectionMatrix = camera.matrices.viewMatrix * camera.matrices.projectionMatrix;
		if (!Engine::SceneGridLayout::BuildVisibleGroundPolygon(camera, 4, 0.0f, 1000.0f, polygon)) {
			return false;
		}
		const auto [perspectiveMinX, perspectiveMaxX] =
			std::minmax_element(polygon.begin(), polygon.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
		if (std::abs(perspectiveMaxX->x - perspectiveMinX->x - 40.0f) >= 0.01f) {
			return false;
		}

		// DXCでShaderと定数配置を確認する
		Engine::DxShaderCompiler compiler{};
		compiler.Init();
		const auto root = Engine::RuntimePaths::GetEngineAssetPath("Shaders/Builtin/Line/AnalyticGrid");
		const auto vertex =
			compiler.CompileShader((root / "analyticGrid.VS.hlsl").wstring(), L"vs_6_0", L"main", Engine::ShaderStage::VS);
		const auto pixel =
			compiler.CompileShader((root / "analyticGrid.PS.hlsl").wstring(), L"ps_6_0", L"main", Engine::ShaderStage::PS);
		const auto* constants = Engine::FindConstantBuffer(pixel.reflection, "GridPassConstants");
		return vertex.IsValid() && pixel.IsValid() && constants && constants->size == 400;
	}

	bool TestRenderFeatureRuntimeOverrides() {

		Engine::RenderFeatureRuntimeOverrides& overrides = Engine::RenderFeatureRuntimeOverrides::GetInstance();
		overrides.ResetAll();
		Engine::MaterialParameterValue value{};
		value.value = 0.75f;
		const Engine::UUID passID{101};
		const Engine::MaterialParameterID parameterID = Engine::MaterialParameterID::FromName("ReflectionStrength");
		if (!overrides.SetEnabled(passID, false) || !overrides.SetParameter(passID, parameterID, "ReflectionStrength", value)) {

			return false;
		}
		Engine::MaterialParameterValue textureValue{};
		textureValue.value = Engine::AssetID{1, 2};
		const Engine::MaterialParameterID textureID = Engine::MaterialParameterID::FromName("gNoiseTexture");
		if (!overrides.SetParameter(passID, textureID, "gNoiseTexture", textureValue)) {

			return false;
		}

		const Engine::RenderFeaturePassRuntimeOverride* effect = overrides.Find(passID);
		const Engine::MaterialParameterValue* parameter = effect ? effect->parameters.Find(parameterID) : nullptr;
		const bool valid = effect && effect->enabled.has_value() && !*effect->enabled && parameter &&
						   std::holds_alternative<float>(parameter->value) && std::get<float>(parameter->value) == 0.75f &&
						   effect->textureOverrides.contains("gNoiseTexture") &&
						   effect->textureOverrides.at("gNoiseTexture") == Engine::AssetID{1, 2};
		const bool cleared = overrides.ClearParameter(passID, textureID) &&
							 !effect->textureOverrides.contains("gNoiseTexture") &&
							 overrides.ClearParameter(passID, parameterID) && overrides.ResetPass(passID) &&
							 overrides.SetGroupEnabled("Selective", false) && !overrides.IsGroupEnabled("Selective", true);

		Engine::RenderFeatureProfileAsset profile{};
		Engine::RenderFeaturePassSettings profilePass{};
		profilePass.id = passID;
		profilePass.name = "RuntimeToggle";
		profilePass.material = Engine::AssetID{1, 2};
		profilePass.anchor = Engine::RenderFeatureAnchor::AfterTransparent;
		profilePass.enabled = false;
		profile.passes.emplace_back(profilePass);
		Engine::RenderFeatureProfileRuntime runtime{};
		runtime.Rebuild(profile);
		const bool enabledByScript =
			overrides.SetEnabled(passID, true) &&
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterTransparent, Engine::RenderViewKind::Game).nodes.size() == 1;
		profile.passes.front().enabled = true;
		runtime.Rebuild(profile);
		const bool disabledPassKeepsBypassNode =
			overrides.SetEnabled(passID, false) &&
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterTransparent, Engine::RenderViewKind::Game).nodes.size() == 1;
		const bool visibleEnabled =
			!overrides.IsEnabled(passID, true) && overrides.SetEnabled(passID, true) && overrides.IsEnabled(passID, false);

		// Scriptで有効にした前段Passを別Anchorから参照
		const auto beforeDependency = profile;
		profile.passes.front().enabled = false;
		profile.passes.front().anchor = Engine::RenderFeatureAnchor::BeforeLighting;
		Engine::RenderFeaturePassSettings dependent = profilePass;
		dependent.id = Engine::UUID{9011};
		dependent.name = "RuntimeDependent";
		dependent.enabled = true;
		dependent.anchor = Engine::RenderFeatureAnchor::AfterLighting;
		dependent.sourceKind = Engine::RenderFeatureSourceKind::PassOutput;
		dependent.source.pass = passID;
		profile.passes.push_back(dependent);
		runtime.Rebuild(profile);
		const auto dependencyPlan = runtime.BuildPlan(dependent.anchor, Engine::RenderViewKind::Game);
		if (!dependencyPlan.IsValid() || dependencyPlan.nodes.size() != 1 ||
			dependencyPlan.nodes.front().source.pass != passID) {
			return false;
		}
		overrides.SetEnabled(passID, false);
		if (runtime.BuildPlan(dependent.anchor, Engine::RenderViewKind::Game).IsValid()) {
			return false;
		}
		// 無効で保存されたPassも有効化時に出力名を検証
		overrides.SetEnabled(passID, true);
		profile.passes.back().enabled = false;
		profile.passes.back().source.output = "Missing";
		overrides.SetEnabled(dependent.id, true);
		runtime.Rebuild(profile);
		if (runtime.BuildPlan(dependent.anchor, Engine::RenderViewKind::Game).IsValid()) {
			return false;
		}
		profile = beforeDependency;
		overrides.ResetAll();

		// 最後の有効PassをAnchorごとに自動で選ぶ
		profile.guid = Engine::AssetID{91, 92};
		Engine::RenderFeaturePassSettings second = profile.passes.front();
		second.id = Engine::UUID{102};
		second.name = "AutomaticOutput";
		profile.passes.emplace_back(second);
		Engine::RenderFeaturePassSettings other = second;
		other.id = Engine::UUID{103};
		other.name = "LaterAnchor";
		other.anchor = Engine::RenderFeatureAnchor::BeforeBlit;
		profile.passes.emplace_back(other);
		runtime.Rebuild(profile);
		const bool automatic = runtime.BuildPlan(second.anchor, Engine::RenderViewKind::Game).sceneColorOutput.pass == second.id &&
			runtime.BuildPlan(other.anchor, Engine::RenderViewKind::Game).sceneColorOutput.pass == other.id;
		overrides.SetEnabled(second.id, false);
		const bool previous = runtime.BuildPlan(second.anchor, Engine::RenderViewKind::Game).sceneColorOutput.pass == passID;
		// Viewの切替は保存値や別Anchorを変更しない
		overrides.SetViewEnabled(profile.guid, true, false);
		const bool selectedView = runtime.BuildPlan(second.anchor, Engine::RenderViewKind::Scene).nodes.empty() &&
			!runtime.BuildPlan(other.anchor, Engine::RenderViewKind::Game).nodes.empty() && profile.sceneView;
		overrides.ResetAll();
		// 同じ構成は再利用し、変更後も旧計画の参照を保持する
		const auto retainedPlan = runtime.GetPlan(second.anchor, Engine::RenderViewKind::Game);
		const bool reused = retainedPlan == runtime.GetPlan(second.anchor, Engine::RenderViewKind::Game);
		overrides.SetParameter(second.id, parameterID, "ReflectionStrength", value);
		const bool parameterOnly = retainedPlan == runtime.GetPlan(second.anchor, Engine::RenderViewKind::Game);
		overrides.SetEnabled(second.id, false);
		const auto disabledPlan = runtime.GetPlan(second.anchor, Engine::RenderViewKind::Game);
		const bool changed = disabledPlan != retainedPlan && disabledPlan->sceneColorOutput.pass == passID &&
			retainedPlan->sceneColorOutput.pass == second.id;
		overrides.SetViewEnabled(profile.guid, false, true);
		const bool viewChanged = runtime.GetPlan(second.anchor, Engine::RenderViewKind::Game)->nodes.empty() &&
			!runtime.GetPlan(second.anchor, Engine::RenderViewKind::Scene)->nodes.empty();
		overrides.ResetAll();
		profile.passes.front().name = "ChangedSnapshot";
		runtime.Rebuild(profile);
		const auto rebuiltPlan = runtime.GetPlan(second.anchor, Engine::RenderViewKind::Game);
		const bool rebuilt = rebuiltPlan != retainedPlan && rebuiltPlan->nodes.front().pass->name == "ChangedSnapshot" &&
			retainedPlan->nodes.front().pass->name != "ChangedSnapshot";
		return valid && cleared && enabledByScript && disabledPassKeepsBypassNode && visibleEnabled && automatic && previous &&
			selectedView && reused && parameterOnly && changed && viewChanged && rebuilt && overrides.Find(passID) == nullptr;
	}

	bool TestShaderPathDependencies() {

		TestDirectory directory("RenderFeatureDependencies", Engine::RuntimePaths::GetGameAssetsRoot());
		const auto& testRoot = directory.GetPath();
		std::error_code ec;
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
			{"name", "ReloadTest"},
			{"sourceShader", Engine::RuntimePaths::ToAssetPath(testRoot / "reload.CS.hlsl")},
			{"stages", nlohmann::json::array({{
						   {"stage", "CS"},
						   {"file", Engine::RuntimePaths::ToAssetPath(testRoot / "reload.CS.hlsl")},
						   {"entry", "main"},
						   {"profile", "cs_6_0"},
					   }})},
		};
		if (!Engine::JsonAdapter::SaveCanonical(shaderPath, shader)) {
			directory.Remove();
			return false;
		}

		Engine::AssetDatabase database{};
		database.Init();
		const Engine::AssetID sourceID =
			database.ImportOrGet(Engine::RuntimePaths::ToAssetPath(testRoot / "reload.CS.hlsl"), Engine::AssetType::Shader);
		const Engine::AssetID shaderID =
			database.ImportOrGet(Engine::RuntimePaths::ToAssetPath(testRoot / "reload.shader.json"), Engine::AssetType::Shader);
		database.RefreshDependencies(shaderID);
		const std::vector<Engine::AssetID>& dependencies = database.FindDependencies(shaderID);
		const std::vector<Engine::AssetID>& referencers = database.FindReferencers(sourceID);
		bool passed = sourceID && shaderID &&
					  std::find(dependencies.begin(), dependencies.end(), sourceID) != dependencies.end() &&
					  std::find(referencers.begin(), referencers.end(), shaderID) != referencers.end();
		// 派生IDは元グラフへ依存し、独自の参照切れは隠さない
		const auto graph = Engine::CreateDefaultSurfaceShaderGraph("Dependencies");
		passed &= Engine::JsonAdapter::SaveCanonical(testRoot / "test.shadergraph.json", Engine::ToJson(graph));
		const auto graphID = database.ImportOrGet(
			Engine::RuntimePaths::ToAssetPath(testRoot / "test.shadergraph.json"), Engine::AssetType::ShaderGraph);
		auto material = Engine::ShaderGraphArtifactCache::CreateMaterial(graph, graphID);
		const auto artifact = Engine::ShaderGraphArtifactCache::DescribeReferences(graph, graphID);
		Engine::ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
		const Engine::AssetID missingID{123, 456};
		Engine::FindPass(material, Engine::MaterialPassKind::Transparent)->pipeline = missingID;
		passed &= Engine::JsonAdapter::SaveCanonical(testRoot / "test.material.json", Engine::ToJson(material));
		const auto materialID = database.ImportOrGet(
			Engine::RuntimePaths::ToAssetPath(testRoot / "test.material.json"), Engine::AssetType::Material);
		database.RefreshDependencies(materialID);
		const auto& graphDependencies = database.FindDependencies(materialID);
		passed &=
			std::find(graphDependencies.begin(), graphDependencies.end(), graphID) != graphDependencies.end() &&
			std::find(graphDependencies.begin(), graphDependencies.end(), missingID) != graphDependencies.end() &&
			std::find(graphDependencies.begin(), graphDependencies.end(), artifact.opaquePipelineID) ==
				graphDependencies.end() &&
			std::find(graphDependencies.begin(), graphDependencies.end(), artifact.opaqueShaderID) == graphDependencies.end();
		passed &= std::any_of(database.GetIssues().begin(), database.GetIssues().end(), [&](const auto& issue) {
			return issue.assetID == materialID && issue.referencedAssetID == missingID &&
				   issue.type == Engine::AssetDatabaseIssueType::MissingReference;
		});
		passed &= std::none_of(database.GetIssues().begin(), database.GetIssues().end(),
			[&](const auto& issue) { return issue.assetID == materialID && issue.referencedAssetID != missingID; });
		// 元グラフが欠損していれば派生参照も再生成できない
		material.shaderGraph = Engine::AssetID{123, 789};
		passed &= Engine::JsonAdapter::SaveCanonical(testRoot / "test.material.json", Engine::ToJson(material));
		database.RefreshDependencies(materialID);
		const auto& missingDependencies = database.FindDependencies(materialID);
		passed &= std::find(missingDependencies.begin(), missingDependencies.end(), artifact.opaqueShaderID) !=
				  missingDependencies.end();
		directory.Remove();
		return passed && !ec;
	}
}
