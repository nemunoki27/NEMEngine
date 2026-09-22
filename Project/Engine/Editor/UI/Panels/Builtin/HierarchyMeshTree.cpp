#include "HierarchyMeshTree.h"
#include "HierarchyEntityOperations.h"

#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/ReparentEntityCommand.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

#include <unordered_map>

using namespace Engine::HierarchyEntityOperations;

namespace Engine::HierarchyMeshTree {

	// Joint階層を再帰描画する
	void DrawJointNode(const EditorPanelContext& context, ECSWorld& world, const Entity& skinnedEntity,
		int32_t jointIndex, const std::unordered_map<int32_t, std::vector<Entity>>& attachedByJoint,
		const DrawEntityCallback& drawEntity);
}

void Engine::HierarchyMeshTree::DrawSubMeshNodes(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity) {

	// MeshRendererを持っていなければ何もしない
	if (!world.HasComponent<MeshRendererComponent>(entity)) {
		return;
	}
	const std::span<const SubMeshMaterial> subMeshes =
		GetMeshSubMeshes(world, entity);
	if (subMeshes.empty()) {
		return;
	}

	ImGui::PushID("SubMeshesRoot");
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("サブメッシュ", false)) {

		ImGui::Indent();
		for (uint32_t subMeshIndex = 0;
			subMeshIndex < static_cast<uint32_t>(subMeshes.size()); ++subMeshIndex) {

			const auto& subMesh = subMeshes[subMeshIndex];

			// 選択状態
			bool isSelected = context.editorState && context.editorState->IsMeshSubMeshSelected(entity, subMesh.stableID, subMeshIndex);

			ImGui::PushID(static_cast<int>(subMeshIndex));

			// 表示名を決定
			std::string displayName = subMesh.name.empty() ?
				("SubMesh_" + std::to_string(subMesh.sourceSubMeshIndex)) : subMesh.name;
			std::string rowLabel = displayName + "##HierarchySubMeshRow";
			float rowWidth = ImGui::GetContentRegionAvail().x;
			// アクティブでない場合はテキストを薄く表示する
			if (!isSelected) {

				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}

			const bool clicked = ImGui::Selectable(rowLabel.c_str(), isSelected, 0, ImVec2(rowWidth, 0.0f));
			if (!isSelected) {
				ImGui::PopStyleColor();
			}
			if (clicked) {

				context.editorState->SelectMeshSubMesh(entity, subMeshIndex, subMesh.stableID);
			}
			ImGui::PopID();
		}
		ImGui::Unindent();
	}
	ImGui::Unindent();
	ImGui::PopID();
}

void Engine::HierarchyMeshTree::DrawSkinnedMeshNodes(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, const DrawEntityCallback& drawEntity) {

	if (!world.HasComponent<SkinnedAnimationComponent>(entity)) {
		return;
	}
	const SkinnedAnimationRuntimeData* runtime =
		TryGetSkinnedAnimationRuntime(world, entity);
	if (!runtime) {
		return;
	}
	const Skeleton& skeleton = runtime->skeleton;
	if (skeleton.joints.empty()) {
		return;
	}

	// このスキンメッシュへ親子付けされたエンティティをジョイントindexごとに集める
	UUID skinnedLocalFileID{};
	if (world.HasComponent<SceneObjectComponent>(entity)) {
		skinnedLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
	}
	std::unordered_map<int32_t, std::vector<Entity>> attachedByJoint;
	if (skinnedLocalFileID) {
		world.ForEachAliveEntity([&](Entity other) {

			if (!world.HasComponent<JointAttachmentComponent>(other)) {
				return;
			}
			Entity attachedSkinned = Entity::Null();
			Matrix4x4 jointSkeletonSpace{};
			if (!JointAttachmentUtility::ResolveAttachedJoint(
				world, other, attachedSkinned, jointSkeletonSpace) || attachedSkinned != entity) {
				return;
			}
			const auto& attachment = world.GetComponent<JointAttachmentComponent>(other);
			const int32_t jointIndex =
				FindSkeletonJointIndex(skeleton, attachment.jointName);
			if (0 <= jointIndex) {
				attachedByJoint[jointIndex].emplace_back(other);
			}
			});
	}

	ImGui::PushID("SkinnedMeshRoot");
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("スキンメッシュ", false)) {

		ImGui::Indent();
		// ルートジョイントから描画する、rootが無効なら親のいないジョイントを全て描く
		if (skeleton.root >= 0 && skeleton.root < static_cast<int32_t>(skeleton.joints.size())) {
			DrawJointNode(context, world, entity, skeleton.root, attachedByJoint, drawEntity);
		} else {
			for (int32_t i = 0; i < static_cast<int32_t>(skeleton.joints.size()); ++i) {
				if (!skeleton.joints[i].parent) {
					DrawJointNode(context, world, entity, i, attachedByJoint, drawEntity);
				}
			}
		}
		ImGui::Unindent();
	}
	ImGui::Unindent();
	ImGui::PopID();
}

void Engine::HierarchyMeshTree::DrawJointNode(const EditorPanelContext& context, ECSWorld& world,
	const Entity& skinnedEntity, int32_t jointIndex,
	const std::unordered_map<int32_t, std::vector<Entity>>& attachedByJoint, const DrawEntityCallback& drawEntity) {

	if (!world.HasComponent<SkinnedAnimationComponent>(skinnedEntity)) {
		return;
	}
	const SkinnedAnimationRuntimeData* runtime =
		TryGetSkinnedAnimationRuntime(world, skinnedEntity);
	if (!runtime) {
		return;
	}
	const Skeleton& skeleton = runtime->skeleton;
	if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {
		return;
	}
	const Joint& joint = skeleton.joints[jointIndex];

	ImGui::PushID(jointIndex);

	// 子ジョイントまたは親子付けエンティティを持つか
	auto attachedIt = attachedByJoint.find(jointIndex);
	const bool hasAttached = attachedIt != attachedByJoint.end() && !attachedIt->second.empty();
	const bool hasChildren = !joint.children.empty() || hasAttached;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!hasChildren) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	const bool selected = context.editorState && context.editorState->IsJointSelected(skinnedEntity, jointIndex);
	if (selected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	const std::string label = joint.name.empty() ? ("Joint_" + std::to_string(jointIndex)) : joint.name;
	const bool opened = ImGui::TreeNodeEx("##JointNode", flags, "%s", label.c_str());
	if (ImGui::IsItemClicked() && context.editorState) {
		context.editorState->SelectJoint(skinnedEntity, jointIndex);
	}

	// ドロップ目標、別エンティティをこのジョイントへ親子付けする
	if (ImGui::BeginDragDropTarget()) {

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery() && context.CanEditScene()) {

				const Entity dragged = ResolveDraggedEntity(world, payload);
				if (world.IsAlive(dragged) && dragged != skinnedEntity) {

					context.host->ExecuteEditorCommand(
						std::make_unique<ReparentEntityCommand>(dragged, skinnedEntity, joint.name));
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (opened) {

		// 子ジョイントを再帰描画する
		for (int32_t childJoint : joint.children) {
			DrawJointNode(context, world, skinnedEntity, childJoint, attachedByJoint, drawEntity);
		}
		// 親子付けエンティティを実エンティティノードとして表示する
		if (hasAttached) {
			for (const Entity& attached : attachedIt->second) {
				if (world.IsAlive(attached)) {
					drawEntity(attached);
				}
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}
