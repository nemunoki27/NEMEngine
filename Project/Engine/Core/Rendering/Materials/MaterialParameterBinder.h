#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Pipelines/Stage/AutoRootSignatureBuilder.h>

// directX
#include <d3d12.h>

// c++
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

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
		// マテリアルテクスチャ1つ分の解決済みバインド情報
		struct TextureBinding {

			const RootBindingLocation* rootBinding = nullptr;
			AssetID textureID{};
			MaterialParameterSemantic semantic = MaterialParameterSemantic::None;
		};

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
			const PipelineState& pipeline, const MaterialAsset& material,
			const MaterialParameterBufferBuilder::TextureResolver& resolveTexture);

		// マテリアル既定値にエンティティごとのparameterOverridesを重ねてアップロードする
		// Sprite/Text等の個別マテリアル対応で使う、overridesが空なら既定値のみと同じになる
		D3D12_GPU_VIRTUAL_ADDRESS ResolveAndUpload(ID3D12Device* device,
			const PipelineState& pipeline, const MaterialAsset& material,
			const MaterialParameterSet& overrides,
			const MaterialParameterBufferBuilder::TextureResolver& resolveTexture);

		// space2テクスチャのRootBindingとAssetIDを解決する
		std::span<const TextureBinding> ResolveTextures(const PipelineState& pipeline,
			const MaterialAsset& material,
			const MaterialParameterSet* overrides);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		struct CacheKey {

			uint64_t pipelineID = 0;
			uint64_t materialHash = 0;
			uint64_t instanceHash = 0;

			bool operator==(const CacheKey&) const = default;
		};
		struct CacheKeyHasher {

			size_t operator()(const CacheKey& key) const noexcept;
		};
		struct CachedBindingData {

			std::vector<uint8_t> packedParameters{};
			std::vector<TextureBinding> textures{};
			uint64_t lastUsedFrame = 0;
			uint64_t uploadedFrame = 0;
			uint64_t packedFrame = 0;
			D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
			bool parametersValid = false;
			bool texturesValid = false;
		};

		// パイプラインのReflectionレイアウトを取得する
		const MaterialParameterLayout& ResolveLayout(const PipelineState& pipeline);
		// 変更検知済みのキャッシュエントリを取得する
		CachedBindingData& ResolveCacheEntry(const PipelineState& pipeline,
			const MaterialAsset& material,
			const MaterialParameterSet* overrides);

		//--------- variables ----------------------------------------------------

		// パイプライン一意IDごとのレイアウトキャッシュ、reflection解析を毎回しない
		// 破棄後の同アドレス再利用による誤ヒットを避けるためポインタではなくIDで引く
		std::unordered_map<uint64_t, MaterialParameterLayout> layoutCache_{};
		// パラメータとテクスチャの解決済みデータ、内容変更時だけ作り直す
		std::unordered_map<CacheKey, CachedBindingData, CacheKeyHasher> bindingCache_{};
		uint64_t frameIndex_ = 0;
		PostProcessConstantBufferAllocator allocator_{};
	};
} // Engine
