#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxMappedUploadBuffer.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;
	class AssetDatabase;
	class SRVDescriptor;
	struct PrimitiveRendererComponent;
	struct GlobalIlluminationMaterialBinding {

		uint32_t callableIndex = UINT32_MAX;
		uint32_t parametersDescriptor = UINT32_MAX;
	};
	//============================================================================
	//	GlobalIlluminationMaterials class
	//	GraphのCallableと変更しないMaterial定数を保持
	//============================================================================
	class GlobalIlluminationMaterials {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		~GlobalIlluminationMaterials();
		// Materialの実行値を準備する
		GlobalIlluminationMaterialBinding Resolve(GraphicsCore& graphicsCore, AssetDatabase& database,
			RenderAssetLibrary& library, const MaterialAsset& material, const MaterialParameterSet& overrides);
		// Primitiveの形状色を共有定数へ保存
		uint32_t ResolvePrimitiveColor(GraphicsCore& graphicsCore, const PrimitiveRendererComponent& renderer);
		// Pipelineへ使用中のGraphを追加する
		void AppendShaders(ShaderAsset& shader, PipelineVariantDesc& variant,
			PipelineStaticSamplerOverrideSet& samplers) const;
		// 再構築で使用した定数だけを保持
		void BeginBuild();
		// 再構築で使用しなかった定数を回収
		void EndBuild();
		// GPU完了までDescriptorの回収を遅らせる
		void Clear();

		//--------- accessor -----------------------------------------------------

		uint32_t GetShaderCount() const { return static_cast<uint32_t>(graphs_.size()); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct GraphEntry {

			AssetID graph{};
			ShaderAsset shader;
			MaterialParameterLayout layout;
			std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
		};
		struct ParameterEntry {

			AssetID graph{};
			std::vector<uint8_t> bytes;
			DxMappedUploadBuffer buffer;
			uint32_t descriptor = UINT32_MAX;
			bool used = true;
		};
		//--------- variables ----------------------------------------------------

		// Callable番号順のGraphと共有定数
		std::vector<GraphEntry> graphs_;
		std::vector<std::unique_ptr<ParameterEntry>> parameters_;
		// Descriptorの発行と回収の窓口
		SRVDescriptor* descriptors_ = nullptr;

		//--------- functions ----------------------------------------------------

		// 同じ定数を再利用してDescriptorを確定
		uint32_t ResolveParameters(GraphicsCore& graphicsCore, AssetID graph, const std::vector<uint8_t>& bytes);
	};
}
