#include "MaterialParameterBinder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

//============================================================================
//	MaterialParameterBinder classMethods
//============================================================================
void Engine::MaterialParameterBinder::BeginFrame() {

	allocator_.BeginFrame();
}

void Engine::MaterialParameterBinder::Release() {

	allocator_.Release();
	layoutCache_.clear();
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MaterialParameterBinder::ResolveAndUpload(ID3D12Device* device,
	const PipelineState& pipeline, const MaterialAsset& material) {

	// パイプラインごとにレイアウトを一度だけ構築してキャッシュする
	auto found = layoutCache_.find(&pipeline);
	if (found == layoutCache_.end()) {

		MaterialParameterLayout layout{};
		layout.Build(pipeline.GetGraphicsReflection(), "MaterialParameters");
		found = layoutCache_.emplace(&pipeline, std::move(layout)).first;
	}

	const MaterialParameterLayout& layout = found->second;
	// シェーダーがMaterialParameters cbufferを宣言していない場合は何もしない
	if (!layout.IsValid()) {
		return 0;
	}

	// MaterialAssetの値をreflectionのoffsetへ詰める
	const std::vector<uint8_t> bytes = MaterialParameterBufferBuilder::Build(material, layout);
	if (bytes.empty()) {
		return 0;
	}

	const PostProcessConstantBufferAllocation allocation = allocator_.AllocateAndUploadBytes(device, bytes);
	return allocation.gpuAddress;
}
