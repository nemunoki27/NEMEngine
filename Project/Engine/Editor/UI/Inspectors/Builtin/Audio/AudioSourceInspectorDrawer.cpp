#include "AudioSourceInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <filesystem>

//============================================================================
//	AudioSourceInspectorDrawer classMethods
//============================================================================
void Engine::AudioSourceInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

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
	}

	//============================================================================
	//	エディター確認用
	//============================================================================
	if (context.editorContext && context.editorContext->assetDatabase && draft.clip) {

		const AssetDatabase* database = context.editorContext->assetDatabase;
		const std::filesystem::path fullPath = database->ResolveFullPath(draft.clip);
		const std::string pathString = fullPath.string();
		const std::string key = fullPath.stem().string();

		if (ImGui::Button("プレビュー再生", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			Audio* audio = Audio::GetInstance();
			if (audio->EnsureLoaded(pathString)) {
				if (draft.loop) {
					audio->Play(key, draft.volume);
				} else {
					audio->PlayOneShot(key, draft.volume);
				}
			}
		}
		if (ImGui::Button("プレビュー停止", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			Audio::GetInstance()->Stop(key);
		}
		ImGui::Separator();
	}
}
