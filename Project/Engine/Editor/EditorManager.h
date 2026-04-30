#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/External/ImGuiManager.h>
#include <Engine/Editor/EditorContext.h>
#include <Engine/Editor/EditorState.h>
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/Graphics/Render/View/SceneViewCameraController.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Mesh/MeshSubMeshPicker.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

// c++
#include <cstdint>
#include <string>
// imgui
#include <imgui.h>
#include <imgui_internal.h>

namespace Engine {

	// front
	class GraphicsCore;
	class ViewportRenderService;
	class RenderPipelineRunner;

	//============================================================================
	//	EditorSceneRequest structures
	//============================================================================
	enum class EditorSceneRequestType :
		uint8_t {

		None,
		NewScene,
		OpenScene,
		SaveScene,
		SaveAndNewScene,
		SaveAndOpenScene,
	};

	struct EditorSceneRequest {

		EditorSceneRequestType type = EditorSceneRequestType::None;
		AssetID sceneAsset{};
	};

	//============================================================================
	//	EditorManager class
	//	ImGuiを使用してエディタのUI全般を管理するクラス
	//============================================================================
	class EditorManager :
		public IEditorPanelHost {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorManager() = default;
		~EditorManager() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム開始前、終了後の処理
		void BeginFrame(GraphicsCore& graphicsCore, const EditorContext& context);
		void EndFrame(GraphicsCore& graphicsCore, const EditorContext& context,
			const ViewportRenderService* viewportRenderService, const ResolvedRenderView* sceneRenderView);

		// 終了処理
		void Finalize();

		// レイアウトリセット要求
		bool ConsumePlayToggleRequest();
		// シーン操作要求
		EditorSceneRequest ConsumeSceneRequest();
		// アクティブシーンを保存済み状態にする
		void MarkActiveSceneSaved();
		// シーン切り替え後の編集状態をリセットする
		void ResetSceneEditingState();

		// エディタコマンド実行
		bool ExecuteEditorCommand(std::unique_ptr<IEditorCommand> command) override;
		// コマンド履歴操作
		bool UndoEditorCommand() override;
		bool RedoEditorCommand() override;

		// 編集操作
		bool DuplicateSelection() override;
		bool CopySelectionToClipboard() override;
		bool PasteClipboard() override;

		// プレイ/ストップの切り替え要求
		void RequestPlayToggle() override;
		// 新規シーン作成要求
		void RequestNewScene() override;
		// シーンを開く要求
		void RequestOpenScene(AssetID sceneAsset) override;
		// アクティブシーンの保存要求
		void RequestSaveScene() override;

		// シーンビューのメッシュピック処理
		void ExecuteSceneMeshPicking(GraphicsCore& graphicsCore,
			const EditorContext& context, const RenderPipelineRunner& renderPipeline);

		//--------- accessor -----------------------------------------------------

		// エディタの状態の取得
		const EditorLayoutState& GetLayoutState() const { return layoutState_; }
		bool IsActiveSceneDirty() const { return activeSceneDirty_; }

		// シーンビュー用のエディタカメラの状態の取得
		const ManualRenderCameraState& GetSceneViewCameraState() const { return sceneViewCameraController_->GetCameraState(); }
		ManualRenderCameraState& GetSceneViewCameraState() { return sceneViewCameraController_->GetCameraState(); }
		const SceneViewCameraSelection& GetSceneViewCameraSelection() const { return editorState_.sceneViewCamera; }
		SceneViewCameraSelection& GetSceneViewCameraSelection() { return editorState_.sceneViewCamera; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ImGui管理クラス
		ImGuiManager imguiManager_;

		// エディタの状態
		EditorState editorState_{};
		EditorLayoutState layoutState_{};

		// 初期化済みか
		bool initialized_ = false;
		// プレイ/ストップの切り替え要求フラグ
		bool requestTogglePlay_ = false;
		// シーン操作要求
		EditorSceneRequest sceneRequest_{};
		// 未保存確認後に実行するシーン操作要求
		EditorSceneRequest pendingSceneRequest_{};
		// 未保存確認ポップアップを開くか
		bool requestOpenUnsavedPopup_ = false;
		// アクティブシーンに未保存の変更があるか
		bool activeSceneDirty_ = false;

		// 各パネル
		std::vector<std::unique_ptr<IEditorPanel>> panels_;

		// エディタコンテキスト
		const EditorContext* currentRenderContext_ = nullptr;

		// シーンビュー用のエディタカメラ
		std::unique_ptr<SceneViewCameraController> sceneViewCameraController_ = nullptr;

		// シーンビューのメッシュピック処理
		std::unique_ptr<MeshSubMeshPicker> meshSubMeshPicker_{};

		//--------- functions ----------------------------------------------------

		// コマンド実行のためのコンテキストを作成する
		EditorCommandContext MakeCommandContext(const EditorContext& context);
		// 操作ショートカット
		void HandleGlobalShortcuts(const EditorContext& context);
		// シーン操作要求をキューに積む
		void QueueSceneRequest(const EditorSceneRequest& request);
		// 未保存シーンの確認ポップアップを描画する
		void DrawUnsavedScenePopup();
		// シーン操作要求の種類をポップアップ表示用の名前に変換する
		const char* GetSceneRequestActionName(EditorSceneRequestType type) const;
		// 未保存確認の結果をシーン操作要求へ反映する
		void SubmitPendingSceneRequest(bool saveBeforeSubmit);
		// エディタのドッキングスペースを描画する
		void DrawDockSpace();
		// 編集操作の実装
		bool CopySelectionToClipboardInternal(const EditorContext& context);
		// 各フェーズのパネルを描画する
		void DrawPanelsByPhase(const EditorPanelContext& context, EditorPanelPhase phase);
		// シーンビューのマニュアルカメラを更新する
		void UpdateSceneViewManualCamera();
	};
} // Engine
