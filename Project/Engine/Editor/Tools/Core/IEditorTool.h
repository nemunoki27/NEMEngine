#pragma once

#include <Engine/Core/Tools/Core/ITool.h>
#include "EditorToolRenderResources.h"

namespace Engine {

	//============================================================================
	//	IEditorTool class
	//	ImGuiで操作するエディタツールのインターフェース
	//============================================================================
	class IEditorTool :
		public ITool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		IEditorTool() = default;
		~IEditorTool() override = default;

		// ToolPanelからツール描画前に呼び出し、RenderTexture作成に必要な状態を渡す
		void BeginEditorToolFrame(const EditorToolContext& context);

		// ToolPanelからツール描画後に呼び出し、フレーム中だけの参照を捨てる
		void EndEditorToolFrame();

		// ToolPanelの一覧からツールを開く
		virtual void OpenEditorTool() {}
		// 独立したエディタウィンドウを描画する
		virtual void DrawEditorTool(const EditorToolContext& context) = 0;
	protected:
		//============================================================================
		//	protected Methods
		//============================================================================
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

		// ツールの描画資源
		EditorToolRenderResources renderResources_;
	};

	//============================================================================
	//	IEditorTool templateMethods
	//============================================================================
	template<typename RenderFunc>
	inline void IEditorTool::RenderToTexture(EditorToolRenderTexture& texture,
		RenderFunc&& renderFunc, const Color4& clearColor) {

		renderResources_.RenderToTexture(texture, std::forward<RenderFunc>(renderFunc), clearColor);
	}
} // Engine
