#include "HierarchyEntityMenu.h"
#include "HierarchyEntityOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/EntityCreationMenu.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/UnpackPrefabCommand.h>
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

using namespace Engine::HierarchyEntityOperations;
using namespace Engine::EntityCreationMenu;

void Engine::HierarchyEntityMenu::Draw(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool activeSelf) {

	// 選択を確定して操作メニューを表示
	if (ImGui::BeginPopup("HierarchyEntityContextMenu")) {

		// 既に複数選択へ含まれているなら維持し、含まれていなければ単体選択にする
		if (!context.editorState->IsEntitySelected(entity)) {
			context.editorState->SelectEntity(entity);
		}

		// アクティブ切り替え、選択中なら全選択へ同じ状態を適用する
		if (ImGui::MenuItem(activeSelf ? "非アクティブにする" : "アクティブにする", nullptr, false,
				context.CanEditScene() || context.IsPlaying())) {

			const bool newActive = !activeSelf;
			SetEntityActiveFromHierarchy(context, world, entity, newActive);
		}

		ImGui::Separator();

		// 子エンティティの種類を階層メニューから選択する
		DrawEntityCreationMenu(context, world.GetUUID(entity), "子にオブジェクトを作成",
			ResolveSceneViewCameraDimension(context.editorState->sceneViewPickDimension));
		if (PrefabInstanceEditUtility::IsPrefabRoot(world, entity)) {

			const bool canUnpack =
				context.CanEditScene() && PrefabInstanceEditUtility::CanUnpack(context.editorContext, world, entity);
			if (ImGui::BeginMenu("プレファブ", canUnpack)) {

				if (ImGui::MenuItem("リンクを解除")) {

					context.host->ExecuteEditorCommand(
						std::make_unique<UnpackPrefabCommand>(entity, PrefabUnpackMode::OutermostRoot));
				}
				if (ImGui::MenuItem("リンクを完全解除")) {

					context.host->ExecuteEditorCommand(
						std::make_unique<UnpackPrefabCommand>(entity, PrefabUnpackMode::Completely));
				}
				ImGui::EndMenu();
			}
		}
		// エンティティを複製
		if (ImGui::MenuItem("複製", "Ctrl+D", false, context.CanEditScene())) {

			context.host->DuplicateSelection();
		}
		// クリップボードにエンティティをコピー
		if (ImGui::MenuItem("コピー", "Ctrl+C", false, context.CanEditScene())) {

			context.host->CopySelectionToClipboard();
		}
		// エンティティを削除、複数選択ならまとめて消す
		const bool canDelete = PrefabInstanceEditUtility::CanDelete(context.editorContext, world, entity);
		if (ImGui::MenuItem("削除", "Del", false, context.CanEditScene() && canDelete)) {

			context.host->DeleteSelection();
		}
		ImGui::EndPopup();
	}
}
