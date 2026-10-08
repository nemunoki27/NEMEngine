#include "ViewportCameraSelection.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

	// カメラ選択の候補
	struct CameraChoice {

		Engine::UUID uuid{};
		std::string label;
	};

	// カメラ選択肢のラベルを作る
	std::string MakeCameraLabel(Engine::ECSWorld& world, Engine::Entity entity, const char* suffix) {

		std::string name = Engine::GetEntityDisplayName(world, entity);
		name += " [";
		name += suffix;
		name += "]";
		return name;
	}
	// ワールドからカメラ選択肢を集める
	template <typename TCamera>
	std::vector<CameraChoice> CollectCameraChoices(Engine::ECSWorld& world, const char* suffix) {

		std::vector<CameraChoice> result{};
		// カメラコンポーネントを持つ全てのエンティティに対して処理
		world.ForEach<TCamera>([&](Engine::Entity entity, TCamera& camera) {
			if (!world.IsAlive(entity)) {
				return;
			}
			if (!camera.common.enabled) {
				return;
			}
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			CameraChoice choice{};
			choice.uuid = world.GetUUID(entity);
			choice.label = MakeCameraLabel(world, entity, suffix);
			result.push_back(std::move(choice));
		});
		std::sort(result.begin(), result.end(),
			[](const CameraChoice& lhs, const CameraChoice& rhs) { return lhs.label < rhs.label; });
		return result;
	}
	// カメラ選択肢から現在の選択のプレビュー文字列を見つける
	std::string FindChoicePreview(
		const std::vector<CameraChoice>& choices, Engine::UUID currentUUID, const char* fallbackLabel) {

		if (!currentUUID) {
			return fallbackLabel;
		}
		for (const auto& choice : choices) {
			if (choice.uuid == currentUUID) {
				return choice.label;
			}
		}
		return fallbackLabel;
	}
	// カメラ選択コンボボックスを描画する
	void DrawCameraChoiceCombo(
		const char* label, Engine::UUID& currentUUID, const std::vector<CameraChoice>& choices, const char* autoLabel) {

		std::string preview = FindChoicePreview(choices, currentUUID, autoLabel);

		if (!ImGui::BeginCombo(label, preview.c_str())) {
			return;
		}

		bool autoSelected = !currentUUID;
		if (ImGui::Selectable(autoLabel, autoSelected)) {
			currentUUID = Engine::UUID{};
		}
		if (autoSelected) {
			ImGui::SetItemDefaultFocus();
		}

		for (const auto& choice : choices) {
			const bool isSelected = (choice.uuid == currentUUID);
			if (ImGui::Selectable(choice.label.c_str(), isSelected)) {
				currentUUID = choice.uuid;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

}

//============================================================================
//	ViewportCameraSelection functions
//============================================================================

void Engine::ViewportCameraSelection::DrawPopup(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return;
	}

	SceneViewCameraSelection& selection = context.editorState->sceneViewCamera;
	std::vector<CameraChoice> orthoChoices = CollectCameraChoices<OrthographicCameraComponent>(*world, "2D");
	std::vector<CameraChoice> perspChoices = CollectCameraChoices<PerspectiveCameraComponent>(*world, "3D");

	if (!ImGui::BeginPopup("##SceneEntityCameraPopup")) {
		return;
	}

	ImGui::PushItemWidth(256.0f);
	DrawCameraChoiceCombo("2D カメラ", selection.orthographicCameraUUID, orthoChoices, "<Auto 2D>");
	DrawCameraChoiceCombo("3D カメラ", selection.perspectiveCameraUUID, perspChoices, "<Auto 3D>");
	ImGui::PopItemWidth();

	ImGui::Separator();

	if (ImGui::Button("選択クリア")) {

		selection.ClearAssignedCameras();
	}
	ImGui::SameLine();
	if (ImGui::Button("デバッグカメラに戻す")) {

		selection.mode = SceneViewCameraMode::DebugManual;
		selection.ClearAssignedCameras();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
