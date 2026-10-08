#include "InspectorSelectionDisplay.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <span>
#include <string>

//============================================================================
//	InspectorSelectionDisplay functions
//============================================================================

void Engine::InspectorSelectionDisplay::DrawSelectedSubMeshHeader(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	// サブメッシュが選択されていることを前提に、サブメッシュの情報を表示する
	if (!context.editorState || !context.editorState->HasValidSubMeshSelection(&world)) {
		return;
	}
	if (!world.HasComponent<MeshRendererComponent>(entity)) {
		return;
	}

	const std::span<const SubMeshMaterial> subMeshes = GetMeshSubMeshes(world, entity);

	// 選択されているサブメッシュのインデックスを取得
	uint32_t subMeshIndex = 0;
	if (!context.editorState->TryResolveSelectedSubMeshIndex(&world, subMeshIndex)) {
		return;
	}
	if (subMeshes.size() <= subMeshIndex) {
		return;
	}

	const auto& subMesh = subMeshes[subMeshIndex];

	// エンティティ名とサブメッシュ名を決定
	std::string entityName =
		world.HasComponent<NameComponent>(entity) ? world.GetComponent<NameComponent>(entity).name : "Entity";
	std::string subMeshName = subMesh.name.empty() ? ("SubMesh_" + std::to_string(subMesh.sourceSubMeshIndex)) : subMesh.name;

	ImGui::TextDisabled("選択対象: サブメッシュ");
	ImGui::Text("所有Entity: %s", entityName.c_str());
	ImGui::Text("サブメッシュ: [%u] %s", subMeshIndex, subMeshName.c_str());

	if (ImGui::Button("Entityへ戻る")) {

		context.editorState->SelectEntity(entity);
	}
	ImGui::Spacing();
	ImGui::Separator();
}

void Engine::InspectorSelectionDisplay::DrawJointInspector(const EditorPanelContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !context.editorState) {
		return;
	}
	const Entity skinned = context.editorState->selectedJointSkinnedEntity;
	const int32_t jointIndex = context.editorState->selectedJointIndex;
	if (!world->IsAlive(skinned) || !world->HasComponent<SkinnedAnimationComponent>(skinned)) {

		ImGui::TextDisabled("ジョイントが無効です");
		return;
	}
	const SkinnedAnimationRuntimeData* runtime = TryGetSkinnedAnimationRuntime(*world, skinned);
	if (!runtime) {

		ImGui::TextDisabled("ジョイントが無効です");
		return;
	}
	const Skeleton& skeleton = runtime->skeleton;
	if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {

		ImGui::TextDisabled("ジョイントが無効です");
		return;
	}
	const Joint& joint = skeleton.joints[jointIndex];

	// ジョイントの基本情報を出す、リネーム等はしない
	ImGui::Text("Joint : %s", joint.name.empty() ? "(no name)" : joint.name.c_str());
	ImGui::Text("Index : %d", jointIndex);
	if (joint.parent && *joint.parent >= 0 && *joint.parent < static_cast<int32_t>(skeleton.joints.size())) {
		ImGui::Text("親 : %s", skeleton.joints[*joint.parent].name.c_str());
	} else {
		ImGui::TextDisabled("親 : (root)");
	}

	// このジョイントへ親子付けされた子エンティティがあるか調べる
	UUID skinnedLocalFileID{};
	if (world->HasComponent<SceneObjectComponent>(skinned)) {
		skinnedLocalFileID = world->GetComponent<SceneObjectComponent>(skinned).localFileID;
	}
	bool hasAttachedEntity = false;
	if (skinnedLocalFileID) {
		world->ForEachAliveEntity([&](Entity other) {
			if (hasAttachedEntity || !world->HasComponent<JointAttachmentComponent>(other)) {
				return;
			}
			const auto& attachment = world->GetComponent<JointAttachmentComponent>(other);
			const int32_t attachedJointIndex = FindSkeletonJointIndex(skeleton, attachment.jointName);
			if (attachment.skinnedEntityLocalFileID == skinnedLocalFileID && attachedJointIndex == jointIndex) {
				hasAttachedEntity = true;
			}
		});
	}

	// 子エンティティがある場合は、ジョイントのワールド行列をTransformの行列表示と同じ形で出す
	if (hasAttachedEntity) {

		ImGui::Spacing();
		ImGui::Separator();
		Matrix4x4 jointWorld{};
		if (JointAttachmentUtility::GetJointWorldMatrix(*world, skinned, joint.name, jointWorld)) {
			MyGUI::TextMatrix4x4("ワールド行列", jointWorld);
		}
	}
}
