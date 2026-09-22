#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneResult.h>

namespace Engine {

	struct SceneInstance;

	//============================================================================
	//	RenderPickingState class
	//	メイン描画からPickingへ渡す結果と借用参照を保持する
	//============================================================================
	class RenderPickingState {
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		friend class RenderPipelineRunner;

		ID3D12Resource* tlasResource_ = nullptr;
		std::vector<MeshSubMeshPickRecord> pickRecords_{};
		std::vector<uint32_t> pickRecordOffsets_{};
		RenderFrameRequest lastRenderRequest_{};
		const SceneInstance* lastActiveScene_ = nullptr;
	};
}
