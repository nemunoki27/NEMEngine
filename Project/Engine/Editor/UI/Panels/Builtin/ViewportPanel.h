#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Platform/Input/InputTypes.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Debug/DepthVisualizer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

// json
#include <json.hpp>

// c++
#include <memory>
#include <utility>
#include <vector>

namespace Engine {

	// front
	struct GizmoViewportRect;

	//============================================================================
	//	ViewportPanel enum class
	//============================================================================
	// 表示するビューポートの種類
	enum class ViewportPanelKind {

		Game,
		Scene,
	};

	// front
	class TextureUploadService;

	//============================================================================
	//	ViewportPanel class
	//	ビューの表示パネル
	//============================================================================
	class ViewportPanel :
		public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ViewportPanel(const char* windowName, const char* label, ViewportPanelKind kind, TextureUploadService& textureUploadService);
		~ViewportPanel() = default;

		void Draw(const EditorPanelContext& context) override;

		//--------- accessor -----------------------------------------------------

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// ギズモ操作セッションの情報をまとめた構造体
		struct EntityGizmoSession {

			bool active = false;
			UUID entityUUID{};
			TransformComponent beforeTransform{};
		};
		// 複数選択ギズモのセッション、中心ピボットを保持し各エンティティへ相対適用する
		struct MultiEntityGizmoSession {

			bool active = false;
			// ドラッグ中フレーム間で持続する中心ピボット
			TransformComponent pivot{};
			// undo用の操作前トランスフォーム
			std::vector<std::pair<UUID, TransformComponent>> beforeTransforms{};
		};
		// アイコン
		struct IconSet {

			// エンティティ選択機能のオン/オフ
			std::string enablePickKey;
			// エンティティ/サブメッシュを選択するだけ
			std::string noneKey;

			// マニュピレーター
			std::string translateKey;
			std::string rotateKey;
			std::string scaleKey;

			// エンティティ単位かサブメッシュ単位の選択を行うか
			std::string entitySelectKey;
			std::string subMeshSelectKey;

			std::string debugCameraKey;
			std::string entityCameraKey;
			std::string selection2DKey;
			std::string selection3DKey;
			std::string selection2DAnd3DKey;
			std::string drawGridKey;

			// 複数選択ギズモのピボット切り替え用、中心ピボットと各原点
			std::string gizmoCenterPivotKey;
			std::string eachEntityOriginKey;
			// オブジェクトのスナップ操作アイコン
			std::string snapEditEntityKey;
			// プレファブ編集を抜けて通常のシーン表示へ戻る用、編集中だけツール列の最上段に出す
			std::string prefabExitKey;
		};

		//--------- variables ----------------------------------------------------

		std::string windowName_;
		std::string label_;
		ViewportPanelKind kind_ = ViewportPanelKind::Scene;

		ImVec2 viewSize_ = ImVec2(768.0f, 432.0f);

		// ギズモ操作セッションの情報
		EntityGizmoSession entityGizmoSession_{};
		MultiEntityGizmoSession multiGizmoSession_{};

		TextureUploadService* textureUploadService_ = nullptr;

		// 表示アイコン
		IconSet icons_{};

		// アイコンボタンのサイズ
		const ImVec2 buttonSize_ = ImVec2(24.0f, 24.0f);

		// プロジェクトからのドラッグ&ドロップ配置のプレビュー状態
		// ドラッグ中に仮エンティティを作って実際に置きながら見せ、ドロップで確定する
		Entity dropPreviewEntity_ = Entity::Null();
		bool dropPreviewActive_ = false;
		bool dropPreviewIsThreeD_ = false;
		AssetID dropPreviewAsset_{};
		ECSWorld* dropPreviewWorld_ = nullptr;
		// 右クリックで一度キャンセルしたら、そのドラッグが終わるまでプレビューを作らない
		bool dropPreviewCanceled_ = false;

		// GBufferデバッグのDepth表示用、深度を線形化グレースケールへ変換して表示する
		DepthVisualizer depthVisualizer_{};
		std::unique_ptr<MultiRenderTarget> depthVisualizeSurface_;
		uint32_t depthVisualizeWidth_ = 0;
		uint32_t depthVisualizeHeight_ = 0;

		//--------- functions ----------------------------------------------------

		void DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size);

		// プロジェクトからのドラッグ&ドロップ配置を処理する、ドラッグ中プレビューとドロップ確定を扱う
		void HandleAssetDropPlacement(const EditorPanelContext& context, RenderViewKind viewKind,
			const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight, bool imageHovered);
		// マウス位置から配置先のワールド座標を求める、3Dはカメラ光線と地面、2Dは画面のピクセル空間
		Vector3 ComputeDropPosition(const EditorPanelContext& context, RenderViewKind viewKind, bool isThreeD,
			const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight) const;
		// スナップ有効時に配置座標を現在の座標スナップ設定の間隔へ吸着させる、isThreeDで2D/3Dの設定を切り替える
		void ApplyDropSnap(const EditorPanelContext& context, Vector3& position, bool isThreeD) const;
		// 配置プレビューの仮エンティティを破棄する
		void DestroyDropPreview();

		// GBufferデバッグのDepth表示で、ビューの深度を可視化サーフェスへ描いて表示用テクスチャを返す
		const RenderTexture2D* RenderDepthVisualization(const EditorPanelContext& context,
			RenderViewKind viewKind, uint32_t width, uint32_t height);

		// シーンギズモの描画
		void DrawSceneGizmo(const EditorPanelContext& context);
		// 複数選択ギズモの描画、中心ピボットの差分を各エンティティへ個別原点で適用する
		void DrawMultiEntityGizmo(const EditorPanelContext& context, ECSWorld& world,
			const GizmoViewportRect& rect);
		// ギズモ終了
		void FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);
		void FinalizeMultiEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);

		// アイコン読み込み
		void RequestIcons();
		// アイコンのテクスチャIDを取得
		ImTextureID GetTextureID(const std::string& key) const;

		bool DrawIconButton(const char* id, ImTextureID textureID, bool active, const ImVec2& size) const;
		void DrawCameraSection(const EditorPanelContext& context);
		void DrawManipulatorSection(const EditorPanelContext& context);
		// スナップ設定の右クリックポップアップ、SRTのグリッド単位と絶対スナップ、グリッド表示を編集する
		void DrawSnapSettingsPopup(const EditorPanelContext& context);
		void DrawGridSection(const EditorPanelContext& context);
		void DrawEntityCameraPopup(const EditorPanelContext& context);
	};
} // Engine

