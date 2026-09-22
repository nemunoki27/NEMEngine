#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

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
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

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

	bool TestRenderFeatureProfile() {

		Engine::RenderFeatureProfileAsset profile{};
		profile.name = "RenderFeatureTest";

		Engine::RenderFeaturePassSettings ao{};
		ao.id = Engine::UUID{ 11 };
		ao.name = "RTAO";
		ao.type = Engine::RenderFeaturePassType::RayTracing;
		ao.anchor = Engine::RenderFeatureAnchor::BeforeLighting;
		ao.material = Engine::AssetID{ 1, 11 };
		ao.materialPass = Engine::MaterialPassKind::RayTracing;
		ao.outputs.emplace_back(Engine::RenderFeatureOutputSettings{
			.name = "AO",
			.shaderResource = "gAmbientOcclusion",
			.format = Engine::RenderFeatureTextureFormat::R16_FLOAT,
			.widthScale = 0.5f,
			.heightScale = 0.5f,
		});

		Engine::RenderFeaturePassSettings reflection{};
		reflection.id = Engine::UUID{ 12 };
		reflection.name = "Reflection";
		reflection.type = Engine::RenderFeaturePassType::RayTracing;
		reflection.anchor = Engine::RenderFeatureAnchor::AfterLighting;
		reflection.material = Engine::AssetID{ 1, 12 };
		reflection.materialPass = Engine::MaterialPassKind::RayTracing;
		reflection.passInputs.emplace(
			"gAmbientOcclusion",
			Engine::RenderFeatureOutputReference{
				.pass = ao.id,
				.output = "AO",
			});
		reflection.outputs.emplace_back(
			Engine::RenderFeatureOutputSettings{});

		Engine::RenderFeaturePassSettings composite{};
		composite.id = Engine::UUID{ 13 };
		composite.name = "ReflectionComposite";
		composite.anchor = Engine::RenderFeatureAnchor::AfterLighting;
		composite.material = Engine::AssetID{ 1, 13 };
		composite.sourceKind = Engine::RenderFeatureSourceKind::SceneColor;
		composite.passInputs.emplace(
			"gReflectionColor",
			Engine::RenderFeatureOutputReference{
				.pass = reflection.id,
				.output = "Color",
			});
		composite.outputs.emplace_back(
			Engine::RenderFeatureOutputSettings{});
		composite.sceneColorOutput = true;
		const Engine::MaterialParameterID thresholdID =
			Engine::MaterialParameterID::FromUUID(Engine::UUID{ 31 });
		Engine::MaterialParameterValue threshold{};
		threshold.value = 0.25f;
		composite.parameterOverrides.Set(thresholdID, "Threshold",
			Engine::MaterialParameterSemantic::None, threshold);
		profile.passes = { ao, reflection, composite };
		profile.hierarchy = {
			Engine::RenderFeatureHierarchyItem{
				.type = Engine::RenderFeatureHierarchyItemType::Pass,
				.id = ao.id,
			},
			Engine::RenderFeatureHierarchyItem{
				.type = Engine::RenderFeatureHierarchyItemType::Group,
				.id = Engine::UUID{ 21 },
				.name = "Reflection",
				.children = {
					Engine::RenderFeatureHierarchyItem{
						.type = Engine::RenderFeatureHierarchyItemType::Pass,
						.id = reflection.id,
					},
					Engine::RenderFeatureHierarchyItem{
						.type = Engine::RenderFeatureHierarchyItemType::Pass,
						.id = composite.id,
					},
				},
			},
		};

		const nlohmann::json data =
			Engine::RenderFeatureProfileSerializer::ToJson(profile);
		Engine::RenderFeatureProfileAsset restored =
			Engine::RenderFeatureProfileSerializer::FromJson(data);
		const Engine::MaterialParameterValue* restoredThreshold =
			restored.passes.size() == 3 ?
				restored.passes[2].parameterOverrides.Find(thresholdID) : nullptr;
		if (restored.name != profile.name || restored.passes.size() != 3 ||
			restored.hierarchy.size() != 2 ||
			restored.hierarchy[1].children.size() != 2 ||
			restored.passes[0].outputs[0].format !=
				Engine::RenderFeatureTextureFormat::R16_FLOAT ||
			restored.passes[1].passInputs.at("gAmbientOcclusion").pass !=
				ao.id || restored.passes[2].sourceKind !=
				Engine::RenderFeatureSourceKind::SceneColor ||
			!restoredThreshold ||
			!std::holds_alternative<float>(restoredThreshold->value) ||
			std::get<float>(restoredThreshold->value) != 0.25f) {

			return false;
		}

		Engine::RenderFeatureProfileRuntime runtime{};
		runtime.Rebuild(restored);

		Engine::RenderFeatureProfileAsset destination{};
		destination.guid = Engine::AssetID{ 91, 92 };
		destination.name = "DestinationProfile";
		destination.version = 9u;
		destination.colorPipeline.exposure.manualEV100 = -2.0f;
		Engine::RenderFeatureProfileAsset expectedCopy = restored;
		expectedCopy.guid = destination.guid;
		expectedCopy.name = destination.name;
		expectedCopy.version = destination.version;
		Engine::SynchronizeRenderFeaturePassOrder(expectedCopy);
		Engine::CopyRenderFeatureProfileSettings(destination, restored);
		if (destination.guid != Engine::AssetID{ 91, 92 } ||
			destination.name != "DestinationProfile" ||
			destination.version != 9u ||
			Engine::RenderFeatureProfileSerializer::ToJson(destination) !=
				Engine::RenderFeatureProfileSerializer::ToJson(expectedCopy)) {

			return false;
		}

		const Engine::RenderFeatureExecutionPlan beforeLighting =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::BeforeLighting,
				Engine::RenderViewKind::Game);
		const Engine::RenderFeatureExecutionPlan afterLighting =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		if (!beforeLighting.IsValid() || beforeLighting.nodes.size() != 1 ||
			!afterLighting.IsValid() || afterLighting.nodes.size() != 2 ||
			afterLighting.nodes.front().selectionGroup ||
			afterLighting.nodes.back().selectionGroup ||
			afterLighting.nodes.front().selectionBegin ||
			afterLighting.nodes.back().selectionEnd ||
			afterLighting.nodes.back().source.pass ||
			afterLighting.sceneColorOutput.pass != composite.id) {

			return false;
		}
		restored.hierarchy[1].enabled = false;
		runtime.Rebuild(restored);
		const Engine::RenderFeatureExecutionPlan disabledHierarchyPlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		if (!disabledHierarchyPlan.nodes.empty() ||
			disabledHierarchyPlan.sceneColorOutput.pass) {

			return false;
		}

		profile.hierarchy[1].selection =
			Engine::RenderFeatureSelectionSettings{
				.mode = Engine::RenderFeatureSelectionMode::MaskedSceneColor,
				.anchor = Engine::RenderFeatureAnchor::AfterLighting,
				.renderingLayerMask = 1u << 3,
				.phaseMask = Engine::MakeRenderFeaturePhaseMask(
					Engine::RenderPhase::Opaque),
				.rendererMask = Engine::RenderFeatureRendererMask::Mesh,
			};
		const nlohmann::json selectiveData =
			Engine::RenderFeatureProfileSerializer::ToJson(profile);
		Engine::RenderFeatureProfileAsset selectiveProfile =
			Engine::RenderFeatureProfileSerializer::FromJson(selectiveData);
		Engine::RenderFeatureProfileAsset postProcessUISource = profile;
		postProcessUISource.passes[0].anchor =
			Engine::RenderFeatureAnchor::AfterPostProcessUI;
		const nlohmann::json postProcessUIData =
			Engine::RenderFeatureProfileSerializer::ToJson(postProcessUISource);
		const Engine::RenderFeatureProfileAsset postProcessUIProfile =
			Engine::RenderFeatureProfileSerializer::FromJson(postProcessUIData);
		if (postProcessUIProfile.passes.empty() ||
			postProcessUIProfile.passes[0].anchor !=
				Engine::RenderFeatureAnchor::AfterPostProcessUI ||
			postProcessUIData["passes"][0].value(
				"anchor", std::string{}) != "AfterPostProcessUI") {

			return false;
		}
		// トーンマッピング後の実行位置を保存し、既存の位置から独立して実行する
		Engine::RenderFeatureProfileAsset afterToneMapSource{};
		Engine::RenderFeaturePassSettings afterToneMapPass = profile.passes[0];
		afterToneMapPass.anchor = Engine::RenderFeatureAnchor::AfterToneMap;
		afterToneMapPass.type = Engine::RenderFeaturePassType::Compute;
		afterToneMapPass.materialPass = Engine::MaterialPassKind::PostProcess;
		afterToneMapPass.outputs = { Engine::RenderFeatureOutputSettings{} };
		afterToneMapPass.sceneColorOutput = true;
		afterToneMapSource.passes = { afterToneMapPass };
		const nlohmann::json afterToneMapData =
			Engine::RenderFeatureProfileSerializer::ToJson(afterToneMapSource);
		const Engine::RenderFeatureProfileAsset afterToneMapProfile =
			Engine::RenderFeatureProfileSerializer::FromJson(afterToneMapData);
		if (afterToneMapProfile.passes.size() != 1u ||
			afterToneMapProfile.passes[0].anchor != Engine::RenderFeatureAnchor::AfterToneMap ||
			afterToneMapData["passes"][0].value("anchor", std::string{}) != "AfterToneMap") {
			return false;
		}
		runtime.Rebuild(afterToneMapProfile);
		const Engine::RenderFeatureExecutionPlan afterToneMapPlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterToneMap,
				Engine::RenderViewKind::Game);
		if (!afterToneMapPlan.IsValid() || afterToneMapPlan.nodes.size() != 1u ||
			afterToneMapPlan.sceneColorOutput.pass != afterToneMapPass.id ||
			!runtime.BuildPlan(Engine::RenderFeatureAnchor::BeforeBlit,
				Engine::RenderViewKind::Game).nodes.empty()) {
			return false;
		}
		runtime.Rebuild(selectiveProfile);
		const Engine::RenderFeatureExecutionPlan selectivePlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		Engine::RenderItem selectedItem{};
		selectedItem.backendID = Engine::RenderBackendID::Mesh;
		selectedItem.renderPhase = Engine::RenderPhase::Opaque;
		selectedItem.renderingLayerMask = 1u << 3;
		if (!selectivePlan.IsValid() || selectivePlan.nodes.size() != 2u ||
			!selectivePlan.nodes.front().selectionBegin ||
			!selectivePlan.nodes.back().selectionEnd ||
			!Engine::MatchesRenderFeatureSelection(selectedItem,
				selectiveProfile.hierarchy[1].selection)) {

			return false;
		}
		selectedItem.renderPhase = Engine::RenderPhase::ScreenUI;
		if (Engine::MatchesRenderFeatureSelection(selectedItem,
			selectiveProfile.hierarchy[1].selection)) {

			return false;
		}

		Engine::RenderFeatureProfileAsset standaloneProfile = profile;
		standaloneProfile.hierarchy[0].selection =
			Engine::RenderFeatureSelectionSettings{
				.mode = Engine::RenderFeatureSelectionMode::MaskedSceneColor,
				.anchor = Engine::RenderFeatureAnchor::BeforeLighting,
				.renderingLayerMask = 1u << 4,
				.phaseMask = Engine::MakeRenderFeaturePhaseMask(
					Engine::RenderPhase::Opaque),
				.rendererMask = Engine::RenderFeatureRendererMask::Mesh,
			};
		standaloneProfile = Engine::RenderFeatureProfileSerializer::FromJson(
			Engine::RenderFeatureProfileSerializer::ToJson(standaloneProfile));
		runtime.Rebuild(standaloneProfile);
		const Engine::RenderFeatureExecutionPlan standalonePlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::BeforeLighting,
				Engine::RenderViewKind::Game);
		if (!standalonePlan.IsValid() || standalonePlan.nodes.size() != 1u ||
			!standalonePlan.nodes.front().selectionGroup ||
			standalonePlan.nodes.front().selectionGroup->type !=
				Engine::RenderFeatureHierarchyItemType::Pass ||
			!standalonePlan.nodes.front().selectionBegin ||
			!standalonePlan.nodes.front().selectionEnd ||
			standaloneProfile.hierarchy[0].selection.mode !=
				Engine::RenderFeatureSelectionMode::MaskedSceneColor) {

			return false;
		}

		selectiveProfile.hierarchy[1].selection.mode =
			Engine::RenderFeatureSelectionMode::IsolatedLayer;
		selectiveProfile.hierarchy[1].selection.phaseMask =
			Engine::MakeRenderFeaturePhaseMask(
				Engine::RenderPhase::Transparent);
		selectedItem.renderPhase = Engine::RenderPhase::Transparent;
		runtime.Rebuild(selectiveProfile);
		if (!runtime.IsItemIsolated(selectedItem)) {
			return false;
		}
		Engine::RenderFeatureRuntimeOverrides::GetInstance().SetGroupEnabled(
			"Reflection", false);
		const Engine::RenderFeatureExecutionPlan disabledRuntimePlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		if (runtime.IsItemIsolated(selectedItem) ||
			runtime.IsPassHierarchyEnabled(reflection.id) ||
			!disabledRuntimePlan.nodes.empty() ||
			disabledRuntimePlan.sceneColorOutput.pass) {

			return false;
		}
		Engine::RenderFeatureRuntimeOverrides::GetInstance().ResetAll();

		selectiveProfile.passes[1].sceneView = false;
		runtime.Rebuild(selectiveProfile);
		if (runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
			Engine::RenderViewKind::Scene).IsValid()) {

			return false;
		}
		selectiveProfile.passes[2].sceneView = false;
		runtime.Rebuild(selectiveProfile);
		const Engine::RenderFeatureExecutionPlan disabledSceneViewPlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Scene);
		if (!disabledSceneViewPlan.nodes.empty() ||
			disabledSceneViewPlan.sceneColorOutput.pass) {

			return false;
		}

		// 再構築前の計画は元のProfileを保持する
		if (!beforeLighting.profileSnapshot || beforeLighting.nodes.front().pass->id != ao.id) {
			return false;
		}

		profile.passes[0].anchor = Engine::RenderFeatureAnchor::AfterTransparent;
		runtime.Rebuild(profile);
		return !runtime.BuildPlan(
			Engine::RenderFeatureAnchor::AfterLighting,
			Engine::RenderViewKind::Game).IsValid();
	}

	bool TestPostProcessSourceExtension() {

		return Engine::PostProcessAssetGenerator::IsComputeShaderSourcePath(
			"GameAssets/PostProcess/Test.CS.hlsl") &&
			Engine::PostProcessAssetGenerator::IsComputeShaderSourcePath(
				"GameAssets/PostProcess/Test.cs.hlsl") &&
			!Engine::PostProcessAssetGenerator::IsComputeShaderSourcePath(
				"GameAssets/PostProcess/Test.PS.hlsl");
	}
}
