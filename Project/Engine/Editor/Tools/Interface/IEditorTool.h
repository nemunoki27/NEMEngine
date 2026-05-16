#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>
#include <Engine/Core/Tools/ITool.h>
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Editor/Tools/EditorToolContext.h>
#include <Engine/Core/Math/Vector2.h>
#include <Engine/Core/Math/Color.h>

// imgui
#include <imgui.h>
// c++
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	EditorToolRenderTexture structure
	//============================================================================

	// エディタツール専用のRenderTexture。
	// SceneView/GameViewとは別管理にして、ツールのプレビュー描画だけで使用する。
	struct EditorToolRenderTexture {

		// RenderTextureの識別名
		std::string name;
		// 作成時に決めた固定サイズ
		Vector2I size;
		// 色RenderTargetの枚数
		uint32_t colorCount = 1;
		// D3D12のClearRenderTargetViewに渡す色。作成時のClearValueと必ず合わせる。
		Color4 clearColor = Color4::Black();
		// プレビュー対象Entity。描画側で必要な時だけ解決して使用する。
		UUID previewEntityUUID{};

		// 色+深度をまとめた描画先
		std::unique_ptr<MultiRenderTarget> renderTarget;

		// リソースを破棄してデスクリプタを解放する
		void Destroy();

		// 描画先が有効かどうか
		bool IsValid() const { return renderTarget && renderTarget->IsValid(); }

		// RenderTextureの本体を取得する
		MultiRenderTarget* GetRenderTarget() const { return renderTarget.get(); }

		// 色テクスチャを取得する
		RenderTexture2D* GetColorTexture(uint32_t index = 0) const;
		// ImGui::Imageへそのまま渡すためのIDを返す
		ImTextureID GetImTextureID(uint32_t index = 0) const;
	};

	//============================================================================
	//	EditorToolRenderContext structure
	//============================================================================

	// RenderToTextureの中で使用する描画コンテキスト
	struct EditorToolRenderContext {

		GraphicsCore* graphicsCore = nullptr;
		GraphicsPlatform* graphicsPlatform = nullptr;
		DxCommand* dxCommand = nullptr;

		EditorToolRenderTexture* renderTexture = nullptr;
		MultiRenderTarget* renderTarget = nullptr;
		const EditorToolContext* toolContext = nullptr;

		// ツール対象のWorldを取得する
		ECSWorld* GetWorld() const { return toolContext ? toolContext->GetWorld() : nullptr; }
	};

	//============================================================================
	//	IEditorTool class
	//	ImGuiで操作するエディタツールのインターフェース
	//============================================================================
	class IEditorTool :
		public ITool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IEditorTool() = default;
		~IEditorTool() override { ClearRenderTextures(); }

		// ToolPanelからツール描画前に呼び出し、RenderTexture作成に必要な状態を渡す
		void BeginEditorToolFrame(const EditorToolContext& context);

		// ToolPanelからツール描画後に呼び出し、フレーム中だけの参照を捨てる
		void EndEditorToolFrame();

		// ToolPanelの一覧からツールを開く
		virtual void OpenEditorTool() {}
		// 独立したエディタウィンドウを描画する
		virtual void DrawEditorTool(const EditorToolContext& context) = 0;
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		// ツール専用RenderTextureを作成する。同じ名前がある場合は既存のものを返す。
		EditorToolRenderTexture* CreateRenderTexture(const std::string& name,
			const Vector2I& size, const Color4& clearColor = Color4::Black(), uint32_t colorCount = 1);

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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelからDrawEditorTool中だけ渡される参照
		GraphicsCore* currentGraphicsCore_ = nullptr;
		const EditorToolContext* currentToolContext_ = nullptr;

		// ツールが作成したRenderTexture
		std::vector<EditorToolRenderTexture> renderTextures_;
	};

	//============================================================================
	//	IEditorTool templateMethods
	//============================================================================

	template<typename RenderFunc>
	inline void IEditorTool::RenderToTexture(EditorToolRenderTexture& texture,
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

		// 作成時のClearValueと違う色でクリアすると、D3D12の警告ブレーク対象になる。
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
