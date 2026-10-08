#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableVertexBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableIndexBuffer.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <array>
#include <span>

//============================================================================
//	QuadGeometry namespace
//	SpriteとTextで使用する単位矩形
//============================================================================
namespace Engine::QuadGeometry {

	// 単位矩形の頂点とIndexを生成して転送する
	template<typename Vertex>
	void CreateBuffers(ID3D12Device* device, BufferUploadService& uploadService,
		ImmutableVertexBuffer<Vertex>& vertexBuffer, ImmutableIndexBuffer& indexBuffer) {

		// 左下から右上の順で位置とUVを揃える
		const std::array<Vertex, 4> vertices = {{
			{ Vector2(0.0f, 1.0f), Vector2(0.0f, 1.0f) },
			{ Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f) },
			{ Vector2(1.0f, 1.0f), Vector2(1.0f, 1.0f) },
			{ Vector2(1.0f, 0.0f), Vector2(1.0f, 0.0f) },
		}};
		const std::array<uint32_t, 6> indices = { 0, 1, 2, 1, 3, 2 };

		// 固定形状をGPUの静的Bufferへ転送
		vertexBuffer.Create(device, uploadService, std::span<const Vertex>(vertices));
		indexBuffer.Create(device, uploadService, std::span<const uint32_t>(indices));
		uploadService.SubmitBatch();
	}
}
