#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

namespace Engine {

	//============================================================================
	//	EditorToolRenderTexture structure
	//============================================================================
	// エディタツール専用のRenderTexture
	// SceneView/GameViewとは別管理にして、ツールのプレビュー描画だけで使用する
	struct EditorToolRenderTexture {

		// RenderTextureの識別名
		std::string name;
		// 作成時に決めた固定サイズ
		Vector2I size;
		// 色RenderTargetの枚数
		uint32_t colorCount = 1;
		// D3D12のClearRenderTargetViewに渡す色で作成時のClearValueと必ず合わせる
		Color4 clearColor = Color4::Black();
		// プレビュー対象Entityで描画側で必要な時だけ解決して使用する
		UUID previewEntityUUID{};

		// 色と必要に応じた深度をまとめた描画先
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

}
