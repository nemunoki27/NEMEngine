#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiManager.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Core/Layout/EditorLayoutManager.h>
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraController.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshPicker.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayPicker.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// c++
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
		EnterPrefabEdit,
		ExitPrefabEdit,
		ExitPrefabEditAll,
		TogglePrefabInContext,
		SavePrefab,
	};

	enum class EditorUnsavedScenePopupResult :
		uint8_t {

		None,
		Save,
		DontSave,
		Cancel,
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
		//============================================================================
		//	public Methods
		//============================================================================

		EditorManager() = default;
		~EditorManager() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム開始前、終了後の処理
		void BeginFrame(GraphicsCore& graphicsCore, const EditorContext& context);
		void EndFrame(GraphicsCore& graphicsCore, const EditorContext& context,
			const ViewportRenderService* viewportRenderService, const ResolvedRenderView* sceneRenderView,
			RenderPipelineRunner* renderPipeline);
		// 外部エディターウィンドウを描画してPresentする
		void RenderPlatformWindows();

		// 終了処理
		void Finalize();

		// レイアウトリセット要求
		bool ConsumePlayToggleRequest();
		bool ConsumePlayResumeRequest();
		bool ConsumePlayPauseRequest();
		bool ConsumePlayFrameStepRequest();
		// シーン操作要求
		EditorSceneRequest ConsumeSceneRequest();
		// 指定シーンを保存済み状態にする
		void MarkSceneSaved(AssetID sceneAsset);
		// 保存開始時の変更世代と一致する場合だけ保存済み状態にする
		void MarkSceneSaved(AssetID sceneAsset,
			uint64_t dirtyRevision);
		// 全シーンを保存済み状態にする
		void MarkAllScenesSaved();
		// シーン切り替え後の編集状態をリセットする
		void ResetSceneEditingState();
		// 読み込み中シーンの未保存状態をリセットする
		void ResetSceneDirtyState();

		// エディタコマンド実行
		bool ExecuteEditorCommand(std::unique_ptr<IEditorCommand> command) override;
		// コマンド履歴操作
		bool UndoEditorCommand() override;
		bool RedoEditorCommand() override;

		// 編集操作
		bool DuplicateSelection() override;
		bool CopySelectionToClipboard() override;
		bool PasteClipboard() override;
		void NotifyEditorCommandPanelFocused(
			EditorCommandPanelKind kind) override;

		// パネル複製要求
		void RequestDuplicatePanel(const std::string& instanceID) override;
		// エディターレイアウト一覧を取得
		const std::vector<EditorLayoutMenuEntry>& GetEditorLayoutEntries() const override;
		const std::string& GetActiveEditorLayoutID() const override;
		bool IsEngineLayoutSaveAvailable() const override;
		// エディターレイアウト操作
		bool RequestSaveEditorLayout(const std::string& name, std::string& outError) override;
		void RequestSaveAllEngineLayouts() override;
		void RequestApplyEditorLayout(const std::string& layoutID) override;
		void RequestDeleteEditorLayout(const std::string& layoutID) override;
		void RequestImportEditorLayouts() override;

		// プレイ/ストップの切り替え要求
		void RequestPlayToggle() override;
		void RequestPlayResume() override;
		void RequestPlayPause() override;
		void RequestPlayFrameStep() override;
		// 新規シーン作成要求
		void RequestNewScene() override;
		// シーンを開く要求
		void RequestOpenScene(AssetID sceneAsset) override;
		// アクティブシーンの保存要求
		void RequestSaveScene() override;
		// アクティブシーンを未保存状態にする
		void RequestMarkSceneDirty() override;
		// プレファブ編集の開始/終了/保存要求
		void RequestEnterPrefabEdit(AssetID prefabAsset) override;
		void RequestExitPrefabEdit() override;
		void RequestExitPrefabEditAll() override;
		void RequestTogglePrefabInContext() override;
		void RequestSavePrefab() override;
		// 終了時の未保存確認ポップアップ表示要求
		void RequestCloseUnsavedScenePopup();
		// 終了時の未保存確認結果
		EditorUnsavedScenePopupResult ConsumeCloseUnsavedScenePopupResult();

		// シーンビューのメッシュピック処理
		void ExecuteSceneMeshPicking(GraphicsCore& graphicsCore,
			const EditorContext& context, RenderPipelineRunner& renderPipeline);
		// SceneView描画前に選択エンティティのデバッグラインを積む
		void DrawSceneDebugObjects(const EditorContext& context);

		//--------- accessor -----------------------------------------------------

		// エディタの状態の取得
		const EditorLayoutState& GetLayoutState() const { return layoutState_; }
		bool IsSceneDirty(AssetID sceneAsset) const;
		uint64_t GetSceneDirtyRevision(
			AssetID sceneAsset) const;
		bool HasDirtyScenes() const { return !dirtySceneAssets_.empty(); }
		const std::unordered_set<AssetID>& GetDirtySceneAssets() const { return dirtySceneAssets_; }

		// シーンビュー用のエディタカメラの状態の取得
		const ManualRenderCameraState& GetSceneViewCameraState() const { return sceneViewCameraController_->GetCameraState(); }
		ManualRenderCameraState& GetSceneViewCameraState() { return sceneViewCameraController_->GetCameraState(); }
		const SceneViewCameraSelection& GetSceneViewCameraSelection() const { return editorState_.sceneViewCamera; }
		SceneViewCameraSelection& GetSceneViewCameraSelection() { return editorState_.sceneViewCamera; }
		bool ShouldDrawSceneViewDefaultGrid() const { return editorState_.drawSceneViewDefaultGrid; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ImGui管理クラス
		ImGuiManager imguiManager_;

		// エディタの状態
		EditorState editorState_{};
		EditorLayoutState layoutState_{};
		EditorLayoutManager editorLayoutManager_{};

		// 初期化済みか
		bool initialized_ = false;
		// プレイ/ストップの切り替え要求フラグ
		bool requestTogglePlay_ = false;
		bool requestResumePlay_ = false;
		bool requestPausePlay_ = false;
		bool requestPlayFrameStep_ = false;
		// シーン操作要求
		EditorSceneRequest sceneRequest_{};
		// 未保存確認後に実行するシーン操作要求
		EditorSceneRequest pendingSceneRequest_{};
		// 未保存確認ポップアップを開くか
		bool requestOpenUnsavedPopup_ = false;
		// 終了時の未保存確認ポップアップを開くか
		bool requestOpenCloseUnsavedPopup_ = false;
		// 終了時の未保存確認結果
		EditorUnsavedScenePopupResult closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
		// 未保存の変更があるシーンアセット
		std::unordered_set<AssetID> dirtySceneAssets_;
		// 非同期保存中の再編集を保存済みにしないためのシーン別変更世代
		std::unordered_map<AssetID, uint64_t> dirtySceneRevisions_;
		uint64_t dirtySceneRevision_ = 0;
		// パネル複製要求
		std::string pendingDuplicatePanelID_;
		// 次のフレーム開始時に適用するレイアウト
		std::optional<EditorLayoutSnapshot> pendingEditorLayout_;
		// ビルトインDefaultドックを再構築するか
		bool requestBuildDefaultDockLayout_ = false;

		// 各パネル
		std::vector<std::unique_ptr<IEditorPanel>> panels_;
		// メイン編集コマンドを受け取るパネルがフォーカス中か
		EditorCommandPanelKind editorCommandPanelKind_ =
			EditorCommandPanelKind::None;

		// エディタコンテキスト
		const EditorContext* currentRenderContext_ = nullptr;

		// シーンビュー用のエディタカメラ
		SceneViewCameraController* sceneViewCameraController_ = nullptr;

		// シーンビューのメッシュピック処理
		std::unique_ptr<MeshSubMeshPicker> meshSubMeshPicker_{};
		SceneComponentOverlayPicker sceneComponentOverlayPicker_{};

		//--------- functions ----------------------------------------------------

		// 2Dエンティティのピック処理を実行
		Entity Execute2DPick(const Vector2& inputPixel, const ResolvedRenderView& view, ECSWorld* world);

		// コマンド実行のためのコンテキストを作成する
		EditorCommandContext MakeCommandContext(const EditorContext& context);
		// 操作ショートカット
		void HandleGlobalShortcuts(const EditorContext& context);
		// シーン操作要求をキューに積む
		void QueueSceneRequest(const EditorSceneRequest& request);
		// 現在編集中のシーンを未保存状態にする
		void MarkCurrentSceneDirty();
		// 未保存シーンの確認ポップアップを描画する
		void DrawUnsavedScenePopup();
		// 終了時の未保存シーン確認ポップアップを描画する
		void DrawCloseUnsavedScenePopup();
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
		// 現在のエディターレイアウトを取得
		EditorLayoutSnapshot CaptureEditorLayout() const;
		// エディターレイアウトを適用
		void ApplyEditorLayout(const EditorLayoutSnapshot& layout, GraphicsCore& graphicsCore);
		// 保留中のエディターレイアウトを適用
		void ApplyPendingEditorLayout(GraphicsCore& graphicsCore);
		// 保留中のパネル複製を適用
		void ApplyPendingPanelDuplicate(const EditorPanelContext& context);
		// 閉じた複製パネルを破棄
		void RemoveClosedDuplicatedPanels();
		// インスタンスIDからパネルを取得
		IEditorPanel* FindPanelByInstanceID(const std::string& instanceID) const;
		// ビルトインDefaultドックを構築
		void BuildDefaultDockLayout(ImGuiID dockSpaceID, const ImVec2& dockSpaceSize);
		// シーンビューのマニュアルカメラを更新する
		void UpdateSceneViewManualCamera();
		// ViewportPanelの表示状態を保存、復元する
		void LoadViewportPanelState();
		void SaveViewportPanelState() const;
	};
} // Engine

