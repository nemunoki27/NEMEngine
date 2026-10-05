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
		overrides.ResetAll();

		// SceneColor出力の切り替えは保存値とパスの有効状態を変更しない
		profile.passes.front().sceneColorOutput = true;
		Engine::RenderFeaturePassSettings second = profile.passes.front();
		second.id = Engine::UUID{102};
		second.name = "RuntimeOutput";
		second.sceneColorOutput = false;
		profile.passes.emplace_back(second);
		Engine::RenderFeaturePassSettings other = second;
		other.id = Engine::UUID{103};
		other.name = "OtherAnchor";
		other.anchor = Engine::RenderFeatureAnchor::BeforeBlit;
		other.sceneColorOutput = true;
		profile.passes.emplace_back(other);
		runtime.Rebuild(profile);
		const bool switched =
			overrides.SetSceneColorOutput(profile, second.id, true) && !overrides.IsSceneColorOutput(passID, true) &&
			overrides.IsSceneColorOutput(second.id, false) && overrides.IsSceneColorOutput(other.id, true) &&
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
							  !overrides.SetSceneColorOutput(profile, Engine::UUID{999}, true) &&
							  overrides.IsSceneColorOutput(second.id, false) && !overrides.IsSceneColorOutput(passID, true);
		const bool resetPass = overrides.ResetPass(second.id) && !overrides.IsSceneColorOutput(second.id, false);
		overrides.ResetAll();
		return valid && cleared && enabledByScript && disabledPassKeepsBypassNode && visibleEnabled && switched && retained &&
			   outputOff && rejected && resetPass && overrides.IsSceneColorOutput(passID, true) &&
			   overrides.Find(passID) == nullptr;
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
