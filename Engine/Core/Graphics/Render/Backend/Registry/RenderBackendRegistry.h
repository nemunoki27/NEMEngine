#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderBackend.h>

// c++
#include <memory>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RenderBackendRegistry class
	//	描画を処理するバックエンドのレジストリ
	//============================================================================
	class RenderBackendRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderBackendRegistry() = default;
		~RenderBackendRegistry() = default;

		// フレーム開始処理
		void BeginFrame(GraphicsCore& graphicsCore);

		// 登録
		void Register(std::unique_ptr<IRenderBackend> backend);

		// データクリア
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 描画IDからバックエンドを検索
		const IRenderBackend* Find(uint32_t id) const;
		IRenderBackend* Find(uint32_t id);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<uint32_t, std::unique_ptr<IRenderBackend>> backends_;
	};
} // Engine