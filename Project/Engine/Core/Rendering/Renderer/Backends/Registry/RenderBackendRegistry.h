#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Foundation/Utility/Registry/RegistryBase.h>

namespace Engine {

	//============================================================================
	//	RenderBackendRegistry class
	//	描画を処理するバックエンドのレジストリ
	//============================================================================
	class RenderBackendRegistry :
		public MapRegistryBase<uint32_t, IRenderBackend> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderBackendRegistry() = default;
		~RenderBackendRegistry() override = default;

		// フレーム開始処理
		void BeginFrame(GraphicsCore& graphicsCore);

		// 登録
		void Register(std::unique_ptr<IRenderBackend> backend);
	};
} // Engine