#pragma once

#include "EditorToolRenderTypes.h"

namespace Engine {

	//============================================================================
	//	EditorToolRenderResources class
	//	編集画面の描画先とフレーム中の描画参照を所有する
	//============================================================================
	class EditorToolRenderResources {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		EditorToolRenderResources() = default;
		~EditorToolRenderResources() { ClearRenderTextures(); }

		// ToolPanelからツール描画前に呼び出し、RenderTexture作成に必要な状態を渡す
		void BeginEditorToolFrame(const EditorToolContext& context);

		// ToolPanelからツール描画後に呼び出し、フレーム中だけの参照を捨てる
		void EndEditorToolFrame();

		// ツール専用RenderTextureを作成し同じ名前がある場合は既存のものを返す
		EditorToolRenderTexture* CreateRenderTexture(const std::string& name,
			const Vector2I& size, const Color4& clearColor = Color4::Black(),
			uint32_t colorCount = 1, bool withDepth = true);

		// 作成済みRenderTextureを名前で取得する
		EditorToolRenderTexture* FindRenderTexture(const std::string& name);

		// 作成済みRenderTextureを名前で取得する
		const EditorToolRenderTexture* FindRenderTexture(const std::string& name) const;

		// 指定名のRenderTextureを破棄する
		void DestroyRenderTexture(const std::string& name);

		// ツールが保持しているRenderTextureをすべて破棄する
		void ClearRenderTextures();

		// RenderTextureを描画先にして、指定された描画関数を実行する
		template <typename RenderFunc>
		void RenderToTexture(EditorToolRenderTexture& texture, RenderFunc&& renderFunc,
			const Color4& clearColor = Color4::Black());

		// 直前のImGuiアイテムをEntityドロップ先として扱い、プレビュー対象を差し替える
		bool AcceptPreviewEntityDragDrop(EditorToolRenderTexture& texture) const;

		// 直前のImGuiアイテムをEntityドロップ先として扱い、プレビュー対象を差し替える
		bool AcceptPreviewEntityDragDrop(const EditorToolContext& context, EditorToolRenderTexture& texture) const;

		// RenderTextureが保持しているプレビュー対象Entityを現在のWorldから解決する
		Entity GetPreviewEntity(const EditorToolRenderTexture& texture) const;

		// RenderTextureが保持しているプレビュー対象Entityを現在のWorldから解決する
		Entity GetPreviewEntity(const EditorToolContext& context, const EditorToolRenderTexture& texture) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelからDrawEditorTool中だけ渡される参照
		GraphicsCore* currentGraphicsCore_ = nullptr;
		const EditorToolContext* currentToolContext_ = nullptr;

		// ツールが作成したRenderTexture
		std::vector<EditorToolRenderTexture> renderTextures_;
	};

	//============================================================================
	//	EditorToolRenderResources templateMethods
	//============================================================================
	template<typename RenderFunc>
	inline void EditorToolRenderResources::RenderToTexture(EditorToolRenderTexture& texture,
		RenderFunc&& renderFunc, const Color4& clearColor) {

		if (!currentGraphicsCore_ || !texture.IsValid()) {
			return;
		}

		auto* dxCommand = currentGraphicsCore_->GetDXObject().GetDxCommand();
		MultiRenderTarget* renderTarget = texture.GetRenderTarget();
		if (!dxCommand || !renderTarget) {
			return;
		}

		// ここでBindしてから呼び出し側の描画を実行する
		renderTarget->TransitionForRender(*dxCommand);
		renderTarget->Bind(*dxCommand);

		// 作成時のClearValueと違う色でクリアすると、D3D12の警告ブレーク対象になる
		const Color4 actualClearColor = texture.clearColor == clearColor ? clearColor : texture.clearColor;

		// レンダーターゲットクリア設定
		MultiRenderTargetClearDesc clearDesc{};
		clearDesc.clearColor = true;
		clearDesc.clearColorValue = actualClearColor;
		clearDesc.clearDepth = true;
		clearDesc.clearDepthValue = 1.0f;
		clearDesc.clearStencil = true;
		clearDesc.clearStencilValue = 0;
		renderTarget->Clear(*dxCommand, clearDesc);

		dxCommand->SetDescriptorHeaps({ currentGraphicsCore_->GetSRVDescriptor().GetDescriptorHeap() });

		// 描画コンテキスト構築
		EditorToolRenderContext renderContext{};
		renderContext.graphicsCore = currentGraphicsCore_;
		renderContext.graphicsPlatform = &currentGraphicsCore_->GetDXObject();
		renderContext.dxCommand = dxCommand;
		renderContext.renderTexture = &texture;
		renderContext.renderTarget = renderTarget;
		renderContext.toolContext = currentToolContext_;

		// 描画部分を呼びだす
		std::forward<RenderFunc>(renderFunc)(renderContext);

		// ImGui::Imageで表示できるように、描画後はShaderResourceへ戻す
		renderTarget->TransitionForShaderRead(*dxCommand);
	}
} // Engine
