#include "AudioSourceInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <filesystem>

//============================================================================
//	AudioSourceInspectorDrawer classMethods
//============================================================================
void Engine::AudioSourceInspectorDrawer::SyncPreviewOwner(ECSWorld* world, const Entity& entity) {

	auto lifetime = world ? world->GetLifetime() : nullptr;
	if (previewWorld_.lock() != lifetime || previewEntity_ != entity ||
		(world && !world->HasComponent<AudioSourceComponent>(entity))) {
		EndPreview();
	}
	previewWorld_ = lifetime;
	previewEntity_ = entity;
}

void Engine::AudioSourceInspectorDrawer::EndPreview() {

	preview_.Stop();
}

void Engine::AudioSourceInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();
	if (previewClip_ != draft.clip) {
		preview_.Stop();
		previewClip_ = draft.clip;
	}

	//============================================================================
	//	再生対象
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("クリップ", draft.clip,
				context.editorContext->assetDatabase, { AssetType::Audio });
			});
	}

	//============================================================================
	//	再生設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("開始時再生", draft.playOnAwake);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("ループ", draft.loop);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("音量", draft.volume,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("空間ブレンド", draft.spatialBlend,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("ピッチ", draft.pitch,
				{ .dragSpeed = 0.01f, .minValue = -3.0f, .maxValue = 3.0f });
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("最小距離", draft.minDistance,
				{ .dragSpeed = 0.1f, .minValue = 0.0001f, .maxValue = 100000.0f });
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("最大距離", draft.maxDistance,
				{ .dragSpeed = 1.0f, .minValue = draft.minDistance, .maxValue = 100000.0f });
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::EnumCombo("減衰", draft.rolloffMode);
		});
	}

	//============================================================================
	//	エディター確認用
	//============================================================================
	if (context.editorContext && context.editorContext->assetDatabase && draft.clip) {

		const AssetDatabase* database = context.editorContext->assetDatabase;
		const std::filesystem::path fullPath = database->ResolveFullPath(draft.clip);

		if (ImGui::Button("プレビュー再生", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			preview_.Play(fullPath, draft.loop, draft.volume, draft.pitch);
		}
		if (ImGui::Button("プレビュー停止", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			preview_.Stop();
		}
		ImGui::Separator();
	}
}
