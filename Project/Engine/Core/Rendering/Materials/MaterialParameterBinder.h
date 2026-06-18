#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>

// directX
#include <d3d12.h>

// c++
#include <string>
#include <unordered_map>

namespace Engine {

	// front
	class PipelineState;

	//============================================================================
	//	MaterialParameterBinder class
	// パイプラインのMaterialParameters cbufferをreflectionし、MaterialAssetの値を
	// 詰めてCBVへアップロードしGPUアドレスを返す、cbuffer未宣言なら何もしない
	// PostProcess以外のSurface/UIマテリアルでもreflection駆動のパラメータを使えるようにする
	//============================================================================
	class MaterialParameterBinder {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		MaterialParameterBinder() = default;
		~MaterialParameterBinder() = default;

		// フレーム開始時にアップロード位置を戻す
		void BeginFrame();
		// アップロードヒープを破棄する
		void Release();

		// 指定パイプラインのMaterialParameters cbufferへmaterialの値を詰めてアップロードする
		// cbufferが無ければ0を返し、呼び出し側はバインドをスキップすればよい
		D3D12_GPU_VIRTUAL_ADDRESS ResolveAndUpload(ID3D12Device* device,
			const PipelineState& pipeline, const MaterialAsset& material);

		// マテリアル既定値にエンティティごとのparameterOverridesを重ねてアップロードする
		// Sprite/Text等の個別マテリアル対応で使う、overridesが空なら既定値のみと同じになる
		D3D12_GPU_VIRTUAL_ADDRESS ResolveAndUpload(ID3D12Device* device,
			const PipelineState& pipeline, const MaterialAsset& material,
			const std::unordered_map<std::string, MaterialParameterValue>& overrides);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// パイプラインごとのレイアウトキャッシュ、reflection解析を毎回しない
		std::unordered_map<const PipelineState*, MaterialParameterLayout> layoutCache_{};
		PostProcessConstantBufferAllocator allocator_{};
	};
} // Engine
