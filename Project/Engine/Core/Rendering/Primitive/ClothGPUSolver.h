#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <cstdint>
#include <memory>

struct ID3D12Resource;

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;
	class PipelineStateCache;
	struct ClothGPUEnvironment;
	struct ClothRuntime;
	struct ClothSettings;
	struct PrimitivePlaneParams;

	class ClothGPUSolver {
	public:
		ClothGPUSolver();
		~ClothGPUSolver();

		bool Update(GraphicsCore& graphics, RenderAssetLibrary& assets, PipelineStateCache& pipelines,
			const ClothRuntime& runtime, const ClothSettings& settings, const PrimitivePlaneParams& plane,
			const Matrix4x4& world, const Matrix4x4& inverse, uint32_t steps, double startTime,
			const ClothGPUEnvironment& environment);

		ID3D12Resource* GetVertexResource() const;
		uint32_t GetVertexDescriptorIndex() const;

	private:
		struct Buffers;
		std::unique_ptr<Buffers> buffers_;
	};
}
