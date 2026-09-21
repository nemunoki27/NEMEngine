#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include "EditorSceneOperations.h"
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/World/Systems/UI/UIInputSystem.h>

using namespace Engine;

void Engine::EngineApplication::HandleEditorSceneRequests() {

	if constexpr (!BuildConfig::kEditorEnabled) {
		return;
	} else {

		// EditorManagerに溜まっているシーン操作要求を1件取り出す
		EditorSceneRequest request = editorManager_.ConsumeSceneRequest();
		if (request.type == EditorSceneRequestType::None) {
			return;
		}
		// Play中はEditWorldを書き換えない
		if (worldManager_.IsPlaying()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: Play中のためシーン操作を無視しました");
			return;
		}

		switch (request.type) {
		case EditorSceneRequestType::NewScene:
			// 空のGameシーンを作成して開く
			CreateNewEditScene();
			break;
		case EditorSceneRequestType::OpenScene:
			// Project上の既存シーンを開く
			OpenEditScene(request.sceneAsset);
			break;
		case EditorSceneRequestType::SaveScene:
			// 現在のEditシーンを保存する
			SaveActiveEditScene();
			break;
		case EditorSceneRequestType::SaveAndNewScene:
			// 読み込み中の全シーンの保存に成功した場合だけ新規シーン作成へ進む
			if (SaveAllEditScenes()) {
				CreateNewEditScene();
			}
			break;
		case EditorSceneRequestType::SaveAndOpenScene:
			// 読み込み中の全シーンの保存に成功した場合だけ別シーンを開く
			if (SaveAllEditScenes()) {
				OpenEditScene(request.sceneAsset);
			}
			break;
		case EditorSceneRequestType::EnterPrefabEdit:
			// 既にPrefab編集中なら、現在の編集内容を保存してから次のPrefabを開く
			if (IsPrefabEditing() && !SaveCurrentPrefab()) {
				break;
			}
			// プレファブを隔離ワールドへ展開して編集モードへ入る、ネストも可
			EnterPrefabEdit(request.sceneAsset);
			break;
		case EditorSceneRequestType::ExitPrefabEdit:
			// 現在のプレファブ編集を保存して1階層戻る
			ExitPrefabEdit();
			break;
		case EditorSceneRequestType::ExitPrefabEditAll:
			// プレファブ編集を一括で抜けて元のシーン編集へ戻る
			ExitAllPrefabEdit();
			break;
		case EditorSceneRequestType::TogglePrefabInContext:
			// In-Context編集のオンオフを切り替える
			TogglePrefabInContextMode();
			break;
		case EditorSceneRequestType::SavePrefab:
			// 現在のプレファブ編集を保存する、退出はしない
			SaveCurrentPrefab();
			break;
		case EditorSceneRequestType::None:
		default:
			break;
		}
	}
}

bool Engine::EngineApplication::CreateNewEditScene() {

	EditorSceneOperationContext context{ assetDatabase_, editScenes_, sceneSystem_, worldManager_, scheduler_,
		systemContext_, editorManager_, activeScene_, activeScenePath_, requestFrameDeltaReset_ };
	return EditorSceneOperations::CreateNewEditScene(context);
}

bool Engine::EngineApplication::OpenEditScene(AssetID sceneAsset) {

	EditorSceneOperationContext context{ assetDatabase_, editScenes_, sceneSystem_, worldManager_, scheduler_,
		systemContext_, editorManager_, activeScene_, activeScenePath_, requestFrameDeltaReset_ };
	return EditorSceneOperations::OpenEditScene(context, sceneAsset);
}

void Engine::EngineApplication::RestoreEditModeUIVisuals() {

	// PlayとPrefabへの切り替え時はOnWorldExitですでに復元済み
	if (!uiInputSystem_ || worldManager_.IsPlaying() || IsPrefabEditing()) {
		return;
	}
	uiInputSystem_->RestoreEditModeVisuals(
		worldManager_.GetEditWorld());
}

void Engine::EngineApplication::AcceptCloseRequest(bool destroyWindow) {

	SaveActiveSceneConfig();
	shutdownAccepted_ = true;
	closeRequestPending_ = false;

	if (destroyWindow) {
		WinApp::RequestCloseWindow();
	}
}

void Engine::EngineApplication::HandleCloseRequestResult() {

	if constexpr (!BuildConfig::kEditorEnabled) {
		return;
	} else {

		if (!closeRequestPending_) {
			return;
		}

		const EditorUnsavedScenePopupResult result = editorManager_.ConsumeCloseUnsavedScenePopupResult();
		switch (result) {
		case EditorUnsavedScenePopupResult::Save:
			if (SaveAllEditScenes()) {
				AcceptCloseRequest(true);
			} else {
				closeRequestPending_ = false;
			}
			break;
		case EditorUnsavedScenePopupResult::DontSave:
			AcceptCloseRequest(true);
			break;
		case EditorUnsavedScenePopupResult::Cancel:
			closeRequestPending_ = false;
			break;
		case EditorUnsavedScenePopupResult::None:
		default:
			break;
		}
	}
}

bool Engine::EngineApplication::RequestClose() {

	if (shutdownAccepted_) {
		return true;
	}

	if constexpr (!BuildConfig::kEditorEnabled) {

		AcceptCloseRequest(false);
		return true;
	} else {

		if (!editorManager_.HasDirtyScenes()) {
			AcceptCloseRequest(false);
			return true;
		}

		// WM_CLOSE中にはImGuiを描画できないため、次のEditorフレームでモーダルを開く
		if (!closeRequestPending_) {
			closeRequestPending_ = true;
			editorManager_.RequestCloseUnsavedScenePopup();
		}
		return false;
	}
}

void Engine::EngineApplication::NotifyAssertBeforeAbort() {

	if (handlingAssertAbort_) {
		return;
	}

	handlingAssertAbort_ = true;
	if constexpr (BuildConfig::kEditorEnabled) {

		// Assert停止直前はImGuiの入力待ちができないため、未保存なら落ちる前に保存しておく
		if (editorManager_.HasDirtyScenes()) {
			SaveAllEditScenes();
		}
	}
	SaveActiveSceneConfig();
	handlingAssertAbort_ = false;
}
