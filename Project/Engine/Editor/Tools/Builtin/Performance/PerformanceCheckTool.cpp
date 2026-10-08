#include "PerformanceCheckTool.h"
#include <Engine/Editor/Commands/Entity/SetPerformanceGridCommand.h>
#include <Engine/Editor/Commands/Entity/PerformanceGridUtility.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
// imgui
#include <imgui.h>

using namespace Engine::PerformanceGridUtility;

//============================================================================
//	PerformanceCheckTool classMethods
//============================================================================

Engine::PerformanceCheckTool::PerformanceCheckTool() {

	LoadSettings();
}

Engine::PerformanceCheckTool::~PerformanceCheckTool() {

	SaveSettings();
}

void Engine::PerformanceCheckTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::PerformanceCheckTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::PerformanceCheckTool::DrawWindow(const EditorToolContext& context) {

	ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("パフォーマンスチェック", &openWindow_)) {
		ImGui::End();
		return;
	}

	ECSWorld* world = context.GetWorld();
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	IEditorPanelHost* host = context.panelContext ? context.panelContext->host : nullptr;
	if (!context.CanEditScene() || !world || !assetDatabase || !host) {
		ImGui::TextDisabled("編集可能なシーンがありません");
		ImGui::End();
		return;
	}

	// World切替後へ古いEntityと統計を持ち越さない
	if (observedWorld_.lock() != world->GetLifetime()) {
		sceneStates_.clear();
		meshVertexCounts_.clear();
		observedWorld_ = world->GetLifetime();
	}

	GridSceneState& sceneState = sceneStates_[context.toolContext.activeSceneInstanceID];
	Entity root = sceneState.rootStableUUID ? world->FindByUUID(sceneState.rootStableUUID) : Entity::Null();
	if (!world->IsAlive(root)) {

		const std::vector<Entity> existingRoots = FindPerformanceGridRoots(*world, context.toolContext.activeSceneInstanceID);
		if (!existingRoots.empty()) {

			root = existingRoots.front();
			sceneState.rootStableUUID = world->GetUUID(root);
		}
	}
	if (root != sceneState.observedRoot || sceneState.renderRevision != world->GetRenderDataRevision() ||
		sceneState.assetRevision != assetDatabase->GetContentRevision()) {
		RefreshStatistics(context, sceneState, root);
	}

	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("PerformanceCheckSettings");
		MyGUI::DragInt("XZグリッド数", gridCountXZ_,
			{
				.minValue = 1,
				.maxValue = kMaxGridCount,
			});
		MyGUI::DragInt("Yグリッド数", gridCountY_,
			{
				.minValue = 1,
				.maxValue = kMaxGridCount,
			});
		MyGUI::DragFloat("グリッド幅", gridWidth_,
			{
				.dragSpeed = 0.1f,
				.minValue = 0.001f,
				.maxValue = 100000.0f,
			});
		MyGUI::AssetReferenceField("モデル", model_, assetDatabase, {AssetType::Mesh});
	}

	if (MyGUI::CollapsingHeader("ポイントライト設定")) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("PerformanceCheckPointLightSettings");
		MyGUI::Checkbox("グリッド間にポイントライトを配置", placePointLights_);
		ImGui::BeginDisabled(!placePointLights_);
		MyGUI::Checkbox("影を有効にする", pointLightShadows_);
		MyGUI::DragInt("配置数", pointLightCount_,
			{
				.minValue = 1,
				.maxValue = static_cast<int32_t>(kMaxEntityCount),
			});
		MyGUI::DragFloat("強度", pointLightIntensity_,
			{
				.dragSpeed = 0.01f,
				.minValue = 0.0f,
				.maxValue = 128.0f,
			});
		MyGUI::DragFloat("半径", pointLightRadius_,
			{
				.dragSpeed = 0.01f,
				.minValue = 0.0f,
				.maxValue = 512.0f,
			});
		MyGUI::DragFloat("減衰", pointLightDecay_,
			{
				.dragSpeed = 0.01f,
				.minValue = 0.0f,
				.maxValue = 512.0f,
			});
		ImGui::EndDisabled();
	}

	ImGui::Spacing();
	const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth = (ImGui::GetContentRegionAvail().x - buttonSpacing) * 0.5f;

	ImGui::BeginDisabled(!model_);
	if (ImGui::Button("配置", ImVec2(buttonWidth, 0.0f))) {

		if (!IsValidGridCount(gridCountXZ_, gridCountY_, placePointLights_, pointLightCount_)) {

			statusMessage_ = "配置できるエンティティ数は1000000までです";
			statusError_ = true;
		} else {

			std::vector<MeshSubMeshLayoutItem> layout;
			MeshAssetAuthoringInfo meshInfo{};
			if (!MeshSubMeshAuthoring::TryBuildLayout(assetDatabase, model_, layout, &meshInfo) || layout.empty()) {

				statusMessage_ = "モデルを解析できません";
				statusError_ = true;
			} else {

				uint64_t vertexCount = 0;
				for (const MeshSubMeshLayoutItem& item : layout) {
					vertexCount += item.vertexCount;
				}
				meshVertexCounts_[model_] = {assetDatabase->GetContentRevision(model_), vertexCount};

				if (!sceneState.rootStableUUID) {
					sceneState.rootStableUUID = UUID::New();
				}
				auto command = std::make_unique<SetPerformanceGridCommand>(sceneState.rootStableUUID, model_, gridCountXZ_,
					gridCountY_, gridWidth_, meshInfo.hasBones, placePointLights_, pointLightShadows_, pointLightCount_,
					pointLightIntensity_, pointLightRadius_, pointLightDecay_, std::move(layout));
				if (host->ExecuteEditorCommand(std::move(command))) {

					statusMessage_ = "グリッドを配置しました";
					statusError_ = false;
					sceneState.observedRoot = Entity::Null();
				} else {

					statusMessage_ = "グリッドを配置できません";
					statusError_ = true;
				}
			}
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	root = sceneState.rootStableUUID ? world->FindByUUID(sceneState.rootStableUUID) : Entity::Null();
	ImGui::BeginDisabled(!world->IsAlive(root));
	if (ImGui::Button("削除", ImVec2(buttonWidth, 0.0f))) {

		if (host->ExecuteEditorCommand(std::make_unique<SetPerformanceGridCommand>(sceneState.rootStableUUID))) {

			statusMessage_ = "グリッドを削除しました";
			statusError_ = false;
			sceneState.observedRoot = Entity::Null();
			sceneState.drawEntityCount = 0;
			sceneState.pointLightCount = 0;
			sceneState.totalVertexCount = 0;
		}
	}
	ImGui::EndDisabled();

	root = sceneState.rootStableUUID ? world->FindByUUID(sceneState.rootStableUUID) : Entity::Null();
	if (root != sceneState.observedRoot || sceneState.renderRevision != world->GetRenderDataRevision() ||
		sceneState.assetRevision != assetDatabase->GetContentRevision()) {
		RefreshStatistics(context, sceneState, root);
	}

	ImGui::Separator();
	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("PerformanceCheckStatistics");
		const FrameProfiler& profiler = FrameProfiler::GetInstance();
		if (MyGUI::BeginPropertyRow("FPS")) {
			ImGui::Text("%.1f", profiler.GetFPS());
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("描画CPU")) {
			ImGui::Text("%.3f ms", profiler.GetAverageMs(FrameProfiler::Category::Draw));
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("GPU合計")) {
			ImGui::Text("%.3f ms", profiler.GetGPUTotalMs());
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("メッシュバッチ更新CPU")) {
			ImGui::Text("%.3f ms", profiler.GetAverageMs(FrameProfiler::Category::MeshBatchUpload));
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("描画エンティティ数")) {
			ImGui::Text("%llu", static_cast<unsigned long long>(sceneState.drawEntityCount));
			MyGUI::EndPropertyRow();
		}
		if (sceneState.pointLightCount > 0 && MyGUI::BeginPropertyRow("ライト数")) {

			ImGui::Text("%llu", static_cast<unsigned long long>(sceneState.pointLightCount));
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("合計頂点数")) {
			ImGui::Text("%llu", static_cast<unsigned long long>(sceneState.totalVertexCount));
			MyGUI::EndPropertyRow();
		}
	}

	if (!statusMessage_.empty()) {
		ImGui::Separator();
		if (statusError_) {
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage_.c_str());
		} else {
			ImGui::TextDisabled("%s", statusMessage_.c_str());
		}
	}
	ImGui::End();
}

void Engine::PerformanceCheckTool::RefreshStatistics(
	const EditorToolContext& context, GridSceneState& state, const Entity& root) {

	ECSWorld* world = context.GetWorld();
	// 表示値と対応するWorldとAssetの世代を記録
	state.renderRevision = world ? world->GetRenderDataRevision() : 0;
	state.assetRevision = context.toolContext.assetDatabase ? context.toolContext.assetDatabase->GetContentRevision() : 0;
	state.observedRoot = root;
	state.drawEntityCount = 0;
	state.pointLightCount = 0;
	state.totalVertexCount = 0;
	if (!world || !world->IsAlive(root)) {
		return;
	}

	const HierarchyComponent* rootHierarchy = world->TryGetComponent<HierarchyComponent>(root);
	if (!rootHierarchy) {
		return;
	}

	Entity child = rootHierarchy->firstChild;
	while (world->IsAlive(child)) {

		const HierarchyComponent* childHierarchy = world->TryGetComponent<HierarchyComponent>(child);
		const Entity next = childHierarchy ? childHierarchy->nextSibling : Entity::Null();

		const MeshRendererComponent* renderer = world->TryGetComponent<MeshRendererComponent>(child);
		if (renderer && renderer->visible) {

			++state.drawEntityCount;
			state.totalVertexCount += ResolveMeshVertexCount(context.toolContext.assetDatabase, renderer->mesh);
		}
		if (world->TryGetComponent<PointLightComponent>(child)) {
			++state.pointLightCount;
		}
		child = next;
	}
}

uint64_t Engine::PerformanceCheckTool::ResolveMeshVertexCount(AssetDatabase* assetDatabase, AssetID meshAssetID) {

	if (!assetDatabase || !meshAssetID) {
		return 0;
	}

	// 同じMeshの更新世代だけ解析結果を再利用
	const uint64_t revision = assetDatabase->GetContentRevision(meshAssetID);
	const auto found = meshVertexCounts_.find(meshAssetID);
	if (found != meshVertexCounts_.end() && found->second.revision == revision) {
		return found->second.count;
	}

	std::vector<MeshSubMeshLayoutItem> layout;
	if (!MeshSubMeshAuthoring::TryBuildLayout(assetDatabase, meshAssetID, layout)) {
		return 0;
	}

	uint64_t vertexCount = 0;
	for (const MeshSubMeshLayoutItem& item : layout) {
		vertexCount += item.vertexCount;
	}
	meshVertexCounts_[meshAssetID] = {revision, vertexCount};
	return vertexCount;
}

void Engine::PerformanceCheckTool::LoadSettings() {

	const std::filesystem::path path = RuntimePaths::GetUserSettingsPath(ConfigPaths::kPerformanceCheckTool);
	if (!JsonAdapter::Check(path)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

	gridCountXZ_ = (std::clamp)(data.value("gridCountXZ", gridCountXZ_), 1, kMaxGridCount);
	gridCountY_ = (std::clamp)(data.value("gridCountY", gridCountY_), 1, kMaxGridCount);
	gridWidth_ = (std::clamp)(data.value("gridWidth", gridWidth_), 0.001f, 100000.0f);
	model_ = ParseAssetID(data, "model");

	placePointLights_ = data.value("placePointLights", placePointLights_);
	pointLightShadows_ = data.value("pointLightShadows", pointLightShadows_);
	pointLightCount_ = (std::clamp)(data.value("pointLightCount", pointLightCount_), 1, static_cast<int32_t>(kMaxEntityCount));
	pointLightIntensity_ = (std::clamp)(data.value("pointLightIntensity", pointLightIntensity_), 0.0f, 128.0f);
	pointLightRadius_ = (std::clamp)(data.value("pointLightRadius", pointLightRadius_), 0.0f, 512.0f);
	pointLightDecay_ = (std::clamp)(data.value("pointLightDecay", pointLightDecay_), 0.0f, 512.0f);
}

void Engine::PerformanceCheckTool::SaveSettings() const {

	nlohmann::json data{};
	data["schemaVersion"] = 2;
	data["gridCountXZ"] = gridCountXZ_;
	data["gridCountY"] = gridCountY_;
	data["gridWidth"] = gridWidth_;
	data["model"] = ToAssetReferenceJson(model_);
	data["placePointLights"] = placePointLights_;
	data["pointLightShadows"] = pointLightShadows_;
	data["pointLightCount"] = pointLightCount_;
	data["pointLightIntensity"] = pointLightIntensity_;
	data["pointLightRadius"] = pointLightRadius_;
	data["pointLightDecay"] = pointLightDecay_;

	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(ConfigPaths::kPerformanceCheckTool), data);
}
