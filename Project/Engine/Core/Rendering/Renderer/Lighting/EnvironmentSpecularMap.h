#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

// c++
#include <array>
#include <memory>
#include <vector>

namespace Engine {

	class GraphicsCore;
	class SRVDescriptor;
	class SkyboxIrradianceMap;

	//============================================================================
	//	EnvironmentSpecularMap class
	//	環境光の粗さ別CubemapとBRDF積分表を生成する
	//============================================================================
	class EnvironmentSpecularMap {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EnvironmentSpecularMap();
		~EnvironmentSpecularMap();

		// 未生成のMipを一段ずつ更新する
		void Update(GraphicsCore& graphics, AssetID source, uint32_t sourceSRV, const TextureOwnership& ownership);
		// 非表示になった環境画像のGPU資源を回収する
		void CollectUnused();

		//--------- accessor -----------------------------------------------------

		uint32_t GetCubemapIndex() const;
		uint32_t GetIrradianceIndex() const;
		uint32_t GetBRDFIndex() const { return brdfSRV_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct Constants {

			uint32_t sourceIndex = UINT32_MAX;
			uint32_t size = 0;
			float roughness = 0.0f;
			uint32_t sampleCount = 128;
		};
		struct Cubemap {

			AssetID source{};
			uint32_t sourceSRV = UINT32_MAX;
			ComPtr<ID3D12Resource> resource;
			uint32_t srv = UINT32_MAX;
			std::array<uint32_t, 8> uavs{};
			uint32_t nextMip = 0;
			uint64_t updateFrame = UINT64_MAX;
			uint64_t lastUsed = 0;
			std::weak_ptr<const uint8_t> sourceLifetime;
			std::unique_ptr<SkyboxIrradianceMap> irradiance;
		};

		//--------- variables ----------------------------------------------------

		static constexpr uint32_t kFaceSize = 128;
		static constexpr uint32_t kMipCount = 8;
		static constexpr uint32_t kBRDFSize = 128;
		std::unique_ptr<PipelineState> prefilterPipeline_;
		std::unique_ptr<PipelineState> brdfPipeline_;
		SRVDescriptor* descriptor_ = nullptr;
		std::vector<std::unique_ptr<Cubemap>> maps_;
		Cubemap* selected_ = nullptr;
		ComPtr<ID3D12Resource> brdf_;
		uint32_t brdfSRV_ = UINT32_MAX;
		ViewConstantBuffer<Constants> constants_;
		PipelineBindingCache bindCache_;
		PipelineBindingCache::SlotID constantsSlot_;
		PipelineBindingCache::SlotID outputSlot_;

		//--------- functions ----------------------------------------------------

		// 生成用Pipelineを準備する
		bool Initialize(GraphicsCore& graphics);
		// 読込元ごとの出力資源を確保する
		std::unique_ptr<Cubemap> CreateCubemap(GraphicsCore& graphics, AssetID source, uint32_t sourceSRV);
		// 積分表を生成する
		void BuildBRDF(GraphicsCore& graphics);
		// 計算定数と出力を接続する
		void Bind(GraphicsCore& graphics, PipelineState& pipeline, const Constants& constants, uint32_t uav);
		// 使用中の出力を回収する
		void Retire(Cubemap& map);
	};
}
