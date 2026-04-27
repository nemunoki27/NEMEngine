#include "AudioSourceInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Audio/Audio.h>
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Utility/ImGui/MyGUI.h>

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
			return MyGUI::AssetReferenceField("Clip", draft.clip,
				context.editorContext->assetDatabase, { AssetType::Audio });
			});
	}

	//============================================================================
	//	再生設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Enabled", draft.enabled);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Play On Awake", draft.playOnAwake);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Loop", draft.loop);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Volume", draft.volume,
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

		if (ImGui::Button("Preview Play", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			Audio* audio = Audio::GetInstance();
			if (audio->EnsureLoaded(pathString)) {
				if (draft.loop) {
					audio->Play(key, draft.volume);
				} else {
					audio->PlayOneShot(key, draft.volume);
				}
			}
		}
		if (ImGui::Button("Preview Stop", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			Audio::GetInstance()->Stop(key);
		}
		ImGui::Separator();
	}
}
