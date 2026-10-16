#include "LightingBakeTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingBake.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingInput.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingStorage.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>
#include <exception>
// imgui
#include <imgui.h>

//============================================================================
//	LightingBakeTool classMethods
//============================================================================
void Engine::LightingBakeTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::LightingBakeTool::StartBake(const EditorToolContext& context) try {

	auto& database = *context.toolContext.assetDatabase;
	auto& baker = context.panelContext->graphicsCore->GetLightingBake();
	sourceScene_ = context.toolContext.activeSceneAsset;
	scenePath_ = database.ResolveFullPath(sourceScene_);
	sceneRevision_ = StorageFileUtility::FileRevision(scenePath_);
	ECSWorld savedWorld;
	SceneSystem scenes;
	const UUID instance = UUID::New();
	if (!scenes.LoadScene(scenePath_, savedWorld, &database, sourceScene_, instance)) {
		status_ = "保存済みシーンを読み込めません";
		return;
	}
	// 保存値から階層とワールド行列を確定する
	SystemContext savedContext;
	savedContext.world = &savedWorld;
	HierarchySystem hierarchy;
	TransformSystem transforms;
	struct TransformSession {
		TransformSystem& transforms;
		ECSWorld& world;
		SystemContext& context;
		~TransformSession() { transforms.OnWorldExit(world, context); }
	} session{transforms, savedWorld, savedContext};
	hierarchy.OnWorldEnter(savedWorld, savedContext);
	transforms.OnWorldEnter(savedWorld, savedContext);
	transforms.LateUpdate(savedWorld, savedContext);
	// 未保存の固定背景を保存済み入力と混在させない
	auto input = BakedLightingInputBuilder::Capture(database, savedWorld, sourceScene_, instance);
	if (!BakedLightingInputBuilder::IsCurrent(input.preparation.data, *context.GetWorld(), context.toolContext.activeSceneInstanceID)) {
		status_ = "固定背景またはライトに未保存の編集があります シーンを保存してください";
		return;
	}
	BakedLightingSettings settings;
	settings.atlasSize = static_cast<uint32_t>(std::clamp(atlasSize_, 64, 2048));
	settings.sampleCount = static_cast<uint32_t>(std::clamp(sampleCount_, 16, 4096));
	settings.bounceCount = static_cast<uint32_t>(std::clamp(bounceCount_, 1, 8));
	settings.captureSize = static_cast<uint32_t>(std::clamp(captureSize_, 16, 128));
	settings.texelsPerFrame = static_cast<uint32_t>(std::clamp(workBudget_, 256, 8192));
	settings.probeSpacing = std::max(probeSpacing_, 0.1f);
	settings.normalBias = std::max(normalBias_, 0.0001f);
	baker.Start(std::move(input), settings, database);
	published_.reset();
	status_.clear();
} catch (const std::exception& error) {
	status_ = error.what();
}

void Engine::LightingBakeTool::Publish(const EditorToolContext& context) try {

	auto& database = *context.toolContext.assetDatabase;
	const auto result = context.panelContext->graphicsCore->GetLightingBake().GetResult();
	if (!result || sourceScene_ != context.toolContext.activeSceneAsset) {
		status_ = "ベイク元のシーンを開いてください";
		return;
	}
	if (StorageFileUtility::FileRevision(scenePath_) != sceneRevision_ ||
		!BakedLightingInputBuilder::IsCurrent(*result, *context.GetWorld(), context.toolContext.activeSceneInstanceID)) {
		status_ = "ベイク中にシーンが変更されています 再ベイクしてください";
		return;
	}
	BakedLightingInputBuilder::ValidateAssets(*result, database, true);
	const auto file = scenePath_.parent_path() / (MakeSceneAssetName(scenePath_) + ".bakedlighting.json");
	// 保存が揃うまでSceneの旧結果を保持する
	const AssetID id = BakedLightingStorage::Save(database, RuntimePaths::ToAssetPath(file), *result, status_);
	if (!id) return;
	auto* instance = context.toolContext.sceneInstances->Find(context.toolContext.activeSceneInstanceID);
	instance->header.bakedLighting = id;
	context.panelContext->host->RequestMarkSceneDirty();
	published_ = result;
	status_ = "ベイク結果を保存しました ベイク照明の使用を有効にしてください";
} catch (const std::exception& error) {
	status_ = error.what();
}

void Engine::LightingBakeTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) return;
	if (!ImGui::Begin(MakeWindowName("照明ベイク").c_str(), &openWindow_)) {
		ImGui::End();
		return;
	}
	if (!context.panelContext || !context.panelContext->graphicsCore || !context.toolContext.sceneInstances ||
		!context.toolContext.assetDatabase || !context.CanEditScene() || !context.toolContext.activeSceneAsset) {
		ImGui::TextDisabled("編集可能なアクティブシーンがありません");
		ImGui::End();
		return;
	}
	auto* instance = context.toolContext.sceneInstances->Find(context.toolContext.activeSceneInstanceID);
	auto& baker = context.panelContext->graphicsCore->GetLightingBake();
	MyGUI::Text("シーン", instance->header.name);
	bool changed = MyGUI::Checkbox("ベイク照明を使用", instance->header.bakedLightingEnabled);
	changed |= MyGUI::AssetReferenceField("保存結果", instance->header.bakedLighting,
		context.toolContext.assetDatabase, {AssetType::BakedLighting}).valueChanged;
	if (changed) context.panelContext->host->RequestMarkSceneDirty();
	ImGui::Separator();
	ImGui::BeginDisabled(baker.IsRunning());
	MyGUI::DragInt("アトラスサイズ", atlasSize_, {.minValue = 64, .maxValue = 2048});
	MyGUI::DragInt("サンプル数", sampleCount_, {.minValue = 16, .maxValue = 4096});
	MyGUI::DragInt("拡散反射回数", bounceCount_, {.minValue = 1, .maxValue = 8});
	MyGUI::DragFloat("プローブ間隔", probeSpacing_, {.minValue = 0.1f});
	MyGUI::DragInt("反射キャプチャサイズ", captureSize_, {.minValue = 16, .maxValue = 128});
	MyGUI::DragInt("フレームの作業量", workBudget_, {.minValue = 256, .maxValue = 8192});
	MyGUI::DragFloat("面のオフセット", normalBias_, {.dragSpeed = 0.001f, .minValue = 0.0001f});
	const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("シーンを保存", ImVec2(width, 0.0f))) context.panelContext->host->RequestSaveScene();
	ImGui::SameLine();
	const bool supported = context.panelContext->graphicsCore->GetDXObject().GetFeatureController().GetSupport().SupportsInlineRayTracingPath();
	ImGui::BeginDisabled(!supported);
	if (ImGui::Button("ベイク", ImVec2(width, 0.0f))) StartBake(context);
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	if (!supported) ImGui::TextDisabled("このGPUではハードウェアベイクを使用できません");
	if (baker.IsRunning()) {
		ImGui::ProgressBar(baker.GetProgress(), ImVec2(-1.0f, 0.0f));
		if (ImGui::Button("中止", ImVec2(-1.0f, 0.0f))) baker.Cancel();
	}
	const auto result = baker.GetResult();
	if (result && result != published_) {
		MyGUI::TextFloat("実際のプローブ間隔", result->probeSpacing);
		if (ImGui::Button("結果を保存", ImVec2(-1.0f, 0.0f))) Publish(context);
	}
	const std::string message = !status_.empty() ? status_ : baker.GetStatus();
	if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
	ImGui::End();
}
