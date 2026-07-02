#include "FillMeshBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <span>

//============================================================================
//	FillMeshBatchResources classMethods
//============================================================================
void Engine::FillMeshBatchResources::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}
	vertices_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	initialized_ = true;
}

void Engine::FillMeshBatchResources::UploadVertices(const std::vector<Vector3>& positions,
	const std::vector<uint32_t>& indices) {

	// インデックスから三角形ごとに頂点を展開する、XZ平面でYは0固定で法線は上向き
	scratch_.clear();
	scratch_.reserve(indices.size());
	for (uint32_t index : indices) {

		if (positions.size() <= index) {
			continue;
		}
		const Vector3& p = positions[index];
		FillMeshVertex vertex{};
		vertex.position = Vector4(p.x, 0.0f, p.z, 1.0f);
		vertex.normal = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
		scratch_.push_back(vertex);
	}

	vertexCount_ = static_cast<uint32_t>(scratch_.size());
	if (vertexCount_ == 0) {
		return;
	}
	vertices_.Upload(std::span<const FillMeshVertex>(scratch_.data(), scratch_.size()));
}
