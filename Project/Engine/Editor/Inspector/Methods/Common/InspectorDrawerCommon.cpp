#include "InspectorDrawerCommon.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Light/PointLightComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Light/SpotLightComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/Graphics/Line/LineRenderer.h>
#include <Engine/Core/Graphics/Render/Light/Interface/ILightExtractor.h>

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
		ImGui::TextDisabled("No registered behavior");
		MyGUI::EndPropertyRow();
		return result;
	}

	// コンボボックスのプレビュー表示は、型が選択されていない場合は"<None>"とする
	const char* preview = type.empty() ? "<None>" : type.c_str();
	if (ImGui::BeginCombo("##Value", preview)) {

		for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

			const auto& info = registry.GetInfo(i);
			if (info.name.empty() || !info.construct) {
				continue;
			}
			const bool selected = (type == info.name);

			if (ImGui::Selectable(info.name.c_str(), selected)) {
				type = info.name;
				result.valueChanged = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

	MyGUI::EndPropertyRow();
	return result;
}

void Engine::InspectorDrawerCommon::DrawEntityDebugObject(ECSWorld& world, const Entity& entity) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// トランスフォームコンポーネントを持っていなければ処理しない
	if (!world.HasComponent<TransformComponent>(entity)) {
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
			camera.farClip, Math::DegToRad(camera.fovY), camera.common.editorFrustumScale, Color::Yellow(), 1.0f);
	}
	// メッシュ
	if (world.HasComponent<MeshRendererComponent>(entity)) {

		// メッシュのバウンディングボックス描画
		renderer3D->DrawOBB(transform.worldMatrix.GetTranslationValue(), transform.localScale,
			transform.localRotation, Color::FromHex(0xff7f00ff), 1.0f);
	}
	// スキニングアニメーション
	if (world.HasComponent<SkinnedAnimationComponent>(entity)) {

		auto& animation = world.GetComponent<SkinnedAnimationComponent>(entity);

		// スケルトンのジョイントを描画
		renderer3D->DrawSkeleton(transform.worldMatrix, animation.runtimeSkeleton);
	}
	// 点光源
	if (world.HasComponent<PointLightComponent>(entity)) {

		auto& pointLight = world.GetComponent<PointLightComponent>(entity);

		// 点光源の影響範囲を描画
		renderer3D->DrawSphere(transform.worldMatrix.GetTranslationValue(), pointLight.radius, pointLight.color, 4, 1.0f);
	}
	// スポットライト
	if (world.HasComponent<SpotLightComponent>(entity)) {

		auto& spotLight = world.GetComponent<SpotLightComponent>(entity);

		// スポットライトの影響範囲を描画
		renderer3D->DrawSpotLightFrustum(transform.worldMatrix.GetTranslationValue(),
			LightExtract::GetWorldDirection(spotLight.direction, transform.worldMatrix),
			spotLight.distance, spotLight.cosAngle, spotLight.cosFalloffStart, spotLight.color);
	}
#else
	// Releaseではエディター用のデバッグライン描画を持たない
	(void)world;
	(void)entity;
#endif
}
