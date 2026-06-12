#include "InspectorDrawerCommon.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Rendering/Renderer/Outline/EditorSelectionOutlineRequestService.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>

//============================================================================
//	InspectorDrawerCommon classMethods
//============================================================================
void Engine::InspectorDrawerCommon::AccumulateEditResult(const ValueEditResult& result,
	bool& anyItemActive, bool& commitRequested) {

	anyItemActive |= result.anyItemActive;
	commitRequested |= result.editFinished;
}

Engine::ValueEditResult Engine::InspectorDrawerCommon::DrawCheckboxField(const char* label, bool& value) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow(label)) {
		return result;
	}

	result.valueChanged = ImGui::Checkbox("##Value", &value);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

	MyGUI::EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::InspectorDrawerCommon::DrawBehaviorTypeField(const char* label, std::string& type) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow(label)) {
		return result;
	}

	// ビヘイビアの型が一つも登録されていない場合は、コンボボックスを表示せずに無効なテキストを表示する
	const auto& registry = BehaviorTypeRegistry::GetInstance();
	if (registry.GetBehaviorTypeCount() == 0) {
		ImGui::TextDisabled("登録済みビヘイビアなし");
		MyGUI::EndPropertyRow();
		return result;
	}

	// 表示はクラス名のみにし識別子としては完全修飾名を保持するため、選択時にtypeへ書くのはinfo.nameのまま
	const auto toShortName = [](const std::string& fullName) -> std::string {
		const size_t dot = fullName.find_last_of('.');
		return dot == std::string::npos ? fullName : fullName.substr(dot + 1);
	};

	// プレビューも短い名前で表示する、未選択は "<None>"
	const std::string preview = type.empty() ? std::string("<None>") : toShortName(type);

	// combo内の絞り込み検索、同時に開くcomboは1つなのでstaticで十分
	static TextSearchFilter typeFilter;

	if (ImGui::BeginCombo("##Value", preview.c_str())) {

		// Add Componentと同じく、上部に検索ボックスを置く
		typeFilter.DrawInput("##BehaviorTypeSearch");
		ImGui::Separator();

		for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

			const auto& info = registry.GetInfo(i);
			if (info.name.empty() || !info.construct) {
				continue;
			}
			const std::string shortName = toShortName(info.name);
			// 短い名前・完全修飾名どちらでも検索一致させる
			if (!typeFilter.Matches(shortName) && !typeFilter.Matches(info.name)) {
				continue;
			}
			const bool selected = (type == info.name);

			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable(shortName.c_str(), selected)) {
				type = info.name;
				result.valueChanged = true;
			}
			// 完全修飾名は曖昧さ解消用にhoverで見せる
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", info.name.c_str());
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	else {
		// comboを閉じたら検索文字を残さない
		typeFilter.Clear();
	}

	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

	MyGUI::EndPropertyRow();
	return result;
}

void Engine::InspectorDrawerCommon::DrawEntityDebugObject(ECSWorld& world, const Entity& entity, int32_t selectionSubMeshIndex) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// トランスフォームコンポーネントが無い、もしくは無効の場合
	if (!world.HasComponent<TransformComponent>(entity) ||
		!world.GetComponent<SceneObjectComponent>(entity).activeInHierarchy) {
		return;
	}
	// トランスフォームを取得
	auto& transform = world.GetComponent<TransformComponent>(entity);

	LineRenderer3D* renderer3D = LineRenderer::GetInstance()->Get3D();

	// 3Dカメラ
	if (world.HasComponent<PerspectiveCameraComponent>(entity)) {

		auto& camera = world.GetComponent<PerspectiveCameraComponent>(entity);

		// カメラフラスタム描画
		renderer3D->DrawCameraFrustum(camera.common.viewMatrix, camera.common.aspectRatio, camera.nearClip,
			camera.farClip, Math::DegToRad(camera.fovY), camera.common.editorFrustumScale, Color4::Yellow(), 1.0f);
	}
	// メッシュ
	if (world.HasComponent<MeshRendererComponent>(entity)) {

		// 選択中メッシュのアウトラインはシーン保存対象にしない
		 ScreenSpaceOutlineStyle style{};
		style.color = Color4::FromHex(0xF02700FF);
		style.widthPixels = 4.0f;
		style.priority = 300;
		style.regionMode = ScreenSpaceOutlineRegionMode::ExteriorPreferred;

		/*ImGui::Begin("MeshOutlineEdit");

		ImGui::ColorEdit4("color", &style.color.r);
		ImGui::DragFloat("widthPixels", &style.widthPixels, 0.01f);

		ImGui::End();*/

		EditorSelectionOutlineRequestService::GetInstance().Request(&world, entity, selectionSubMeshIndex, style);
	}
	// スキニングアニメーション
	if (world.HasComponent<SkinnedAnimationComponent>(entity)) {

		auto& animation = world.GetComponent<SkinnedAnimationComponent>(entity);

		// スケルトンのジョイントを描画
		renderer3D->DrawSkeleton(transform.worldMatrix, animation.runtimeSkeleton);
	}
	// 平行光源
	if (world.HasComponent<DirectionalLightComponent>(entity)) {

		auto& directionalLight = world.GetComponent<DirectionalLightComponent>(entity);
		const Vector3 direction =
			LightExtract::GetWorldDirection(directionalLight.direction, transform.worldMatrix);
		const Quaternion rotation = Quaternion::FromToY(direction);

		// DirectionalLightの向きを矢印で表示する
		renderer3D->DrawArrow(transform.worldMatrix.GetTranslationValue(), 4.0f,
			rotation, directionalLight.color, 1.0f);
	}
	// スポットライト
	if (world.HasComponent<SpotLightComponent>(entity)) {

		auto& spotLight = world.GetComponent<SpotLightComponent>(entity);
		const Vector3 direction =
			LightExtract::GetWorldDirection(spotLight.direction, transform.worldMatrix);
		const Quaternion rotation = Quaternion::FromToY(direction);

		// SpotLightの向きを矢印で表示する
		renderer3D->DrawArrow(transform.worldMatrix.GetTranslationValue(), 4.0f,
			rotation, spotLight.color, 1.0f);
	}
#else
	// Releaseではエディター用のデバッグライン描画を持たない
	(void)world;
	(void)entity;
#endif
}
