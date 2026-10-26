#include "ParticleSourceDrawer.h"

//============================================================================
//	include
//============================================================================
#include "GUI/ParticleGUIHelpers.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>

using namespace Engine;

namespace {

	// 既存の保存参照を保ちながらScene内の候補を選ぶ
	template <typename Component>
	bool DrawReference(const EditorPanelContext& context, const char* label, SceneEntityReference& reference) {

		auto* world = context.GetWorld();
		if (!world || !MyGUI::BeginPropertyRow(label)) return false;
		const auto owner = context.editorState ? context.editorState->selectedEntity : Entity::Null();
		const auto current = reference.Resolve(*world, owner);
		const auto* name = world->TryGetComponent<NameComponent>(current);
		const std::string display = name ? name->name : reference.localFileID ? "参照先なし : " + ToString(reference.localFileID) : "なし";
		bool changed = false;
		if (ImGui::BeginCombo("##Value", display.c_str())) {
			if (ImGui::Selectable("なし", !reference.localFileID && !reference.sourceAsset)) { reference = {}; changed = true; }
			world->ForEach<Component, SceneObjectComponent>([&](Entity entity, [[maybe_unused]] Component& component,
				[[maybe_unused]] SceneObjectComponent& scene) {
				const auto* entityName = world->TryGetComponent<NameComponent>(entity);
				const std::string labelText = entityName ? entityName->name : ToString(world->GetUUID(entity));
				ImGui::PushID(static_cast<int32_t>(entity.index));
				if (ImGui::Selectable(labelText.c_str(), entity == current)) {
					reference = SceneEntityReference::FromEntity(*world, entity);
					changed = true;
				}
				ImGui::PopID();
			});
			ImGui::EndCombo();
		}
		MyGUI::EndPropertyRow();
		return changed;
	}

	// 空の配列は全SubMeshを意味する
	bool DrawSubMeshes(std::vector<uint32_t>& subMeshes) {

		bool changed = false;
		ImGui::TextUnformatted("SubMeshを指定しない場合は全体から発生します");
		for (size_t i = 0; i < subMeshes.size(); ++i) {
			ImGui::PushID(static_cast<int32_t>(i));
			int32_t value = static_cast<int32_t>(subMeshes[i]);
			if (MyGUI::DragInt("SubMesh", value).valueChanged) { subMeshes[i] = static_cast<uint32_t>((std::max)(0, value)); changed = true; }
			ImGui::SameLine();
			if (ImGui::SmallButton("削除")) {
				subMeshes.erase(subMeshes.begin() + i);
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (subMeshes.size() < 1024 && ImGui::Button("SubMeshを追加")) {
			subMeshes.push_back(subMeshes.empty() ? 0 : subMeshes.back() + 1);
			changed = true;
		}
		return changed;
	}

}

bool Engine::ParticleSourceDrawer::Draw(const EditorPanelContext& context, ParticleSourceSettings& settings) {

	using ParticleGUI::MakeDragSetting;
	if (!MyGUI::CollapsingHeader("GPU発生元", false)) return false;
	bool changed = MyGUI::EnumCombo("発生元", settings.kind).valueChanged;
	if (settings.kind == ParticleSourceKind::Analytic) return changed;
	auto* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	AssetEditSetting edit;
	const bool geometry = settings.kind == ParticleSourceKind::MeshVertex || settings.kind == ParticleSourceKind::MeshEdge ||
		settings.kind == ParticleSourceKind::MeshSurface;
	if (geometry) {
		changed |= MyGUI::AssetReferenceField("メッシュ", settings.mesh, database, {AssetType::Mesh}, edit).valueChanged;
		changed |= DrawReference<MeshRendererComponent>(context, "Renderer", settings.renderer);
		changed |= DrawSubMeshes(settings.subMeshes);
	} else if (settings.kind == ParticleSourceKind::Texture) {
		changed |= MyGUI::AssetReferenceField("画像", settings.texture, database, {AssetType::Texture, AssetType::RenderTexture}, edit).valueChanged;
		changed |= MyGUI::DragVector2("画像平面の大きさ", settings.textureSize, MakeDragSetting(0.0001f, 100000)).valueChanged;
	} else changed |= DrawReference<PerspectiveCameraComponent>(context, "Camera", settings.camera);
	changed |= MyGUI::DragFloat("表面からの距離", settings.surfaceOffset).valueChanged;
	changed |= MyGUI::Checkbox("順番に発生", settings.sequential);
	const bool cameraSurface = settings.kind == ParticleSourceKind::CameraSurface;
	if (cameraSurface && (settings.attachToSurface || settings.inheritVelocity != 0)) {
		settings.attachToSurface = false;
		settings.inheritVelocity = 0;
		changed = true;
	}
	ImGui::BeginDisabled(cameraSurface);
	changed |= MyGUI::Checkbox("表面へ固定", settings.attachToSurface);
	changed |= MyGUI::DragFloat("移動速度を継承", settings.inheritVelocity, MakeDragSetting(0, 10)).valueChanged;
	ImGui::EndDisabled();
	if (cameraSurface) ImGui::TextWrapped("Camera表面は同じ表面点を保持できないため、固定と速度継承を使えません");
	changed |= MyGUI::Checkbox("表面色を継承", settings.inheritColor);
	changed |= MyGUI::Checkbox("マスクを使う", settings.useMask);
	if (settings.useMask) {
		if (geometry) changed |= MyGUI::Checkbox("頂点カラーをマスクに使う", settings.maskFromVertexColors);
		if (!geometry || !settings.maskFromVertexColors)
			changed |= MyGUI::AssetReferenceField("マスク画像", settings.maskTexture, database, {AssetType::Texture}, edit).valueChanged;
		changed |= MyGUI::EnumCombo("チャンネル", settings.maskChannel).valueChanged;
		changed |= MyGUI::DragFloat("最小値", settings.maskMinimum, MakeDragSetting(0, 1)).valueChanged;
		changed |= MyGUI::DragFloat("最大値", settings.maskMaximum, MakeDragSetting(settings.maskMinimum, 1)).valueChanged;
	}
	return changed;
}
