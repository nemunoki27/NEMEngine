#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>

namespace Engine {

	//============================================================================
	//	MeshRenderItemExtractor class
	//	メッシュ描画アイテム抽出器
	//============================================================================
	class MeshRenderItemExtractor :
		public IRenderItemExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshRenderItemExtractor() = default;
		~MeshRenderItemExtractor() = default;

		void Extract(ECSWorld& world, RenderSceneBatch& batch) override;
	};
} // Engine