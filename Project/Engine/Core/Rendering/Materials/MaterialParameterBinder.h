#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Pipelines/Stage/RootSignatureLayout.h>

// c++
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

// directX
#include <d3d12.h>

namespace Engine {

	// front
	class PipelineState;

	//============================================================================
	//	MaterialParameterBinder class
	//	Materialの配置と値を解決して描画用Bufferへ転送するクラス
	//============================================================================
	class MaterialParameterBinder {
	public:
		// マテリアルテクスチャ1つ分の解決済みバインド情報
		struct TextureBinding {

			const RootBindingLocation* rootBinding = nullptr; // Pipelineが所有する配置
			AssetID textureID{}; // TextureのAsset ID
			MaterialParameterSemantic semantic = MaterialParameterSemantic::None; // Textureの用途
		};

		//========================================================================
		//	public Methods
		//========================================================================

		MaterialParameterBinder() = default;
		~MaterialParameterBinder() = default;

		// フレーム開始時にアップロード位置を戻す
		void BeginFrame();
		// Texture公開世代が変わった時だけ番号を再解決する
		void SetTextureRevision(uint64_t revision, AssetID renderTextureTarget = {});
		// アップロードヒープを破棄する
		void Release();

		// Materialの既定値を転送し、宣言が無ければ0を返す
		D3D12_GPU_VIRTUAL_ADDRESS ResolveAndUpload(GraphicsResourceRetirement& retirement, ID3D12Device* device,
			const PipelineState& pipeline, const MaterialAsset& material,
			const MaterialParameterBufferBuilder::TextureResolver& resolveTexture);

		// Instanceの上書きを重ねて転送する
		D3D12_GPU_VIRTUAL_ADDRESS ResolveAndUpload(GraphicsResourceRetirement& retirement, ID3D12Device* device,
			const PipelineState& pipeline, const MaterialAsset& material, const MaterialParameterSet& overrides,
			const MaterialParameterBufferBuilder::TextureResolver& resolveTexture);

		// space2テクスチャのRootBindingとAssetIDを解決する
		std::span<const TextureBinding> ResolveTextures(const PipelineState& pipeline,
			const MaterialAsset& material, const MaterialParameterSet* overrides);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// PipelineとMaterialと描画対象の組み合わせ
		struct CacheKey {

			uint64_t pipelineID = 0; // Pipelineの生成番号
			uint64_t materialHash = 0; // 既定値の内容Hash
			uint64_t instanceHash = 0; // 上書き値の内容Hash
			AssetID renderTextureTarget{}; // 参照先の描画Texture

			bool operator==(const CacheKey&) const = default;
		};
		// 解決結果の検索用Hash
		struct CacheKeyHasher {

			size_t operator()(const CacheKey& key) const noexcept;
		};
		// 1つの解決結果とframeごとの転送状態
		struct CachedBindingData {

			std::vector<uint8_t> packedParameters{}; // 宣言配置へ詰めた値
			std::vector<TextureBinding> textures{}; // 解決済みのTexture配置
			uint64_t lastUsedFrame = 0; // 最後に参照されたframe
			uint64_t uploadedFrame = 0; // 転送したframe
			uint64_t packedFrame = 0; // 値を解決したframe
			D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0; // 転送先のGPUアドレス
			bool parametersValid = false; // 値を次のframeも再利用できるか
			bool texturesValid = false; // Texture配置が解決済みか
		};

		//--------- variables ----------------------------------------------------

		// アドレス再利用と区別するPipeline IDごとの配置
		std::unordered_map<uint64_t, MaterialParameterLayout> layoutCache_{};
		// パラメータとテクスチャの解決済みデータ、内容変更時だけ作り直す
		std::unordered_map<CacheKey, CachedBindingData, CacheKeyHasher> bindingCache_{};
		uint64_t textureRevision_ = 0; // Texture公開の更新番号
		AssetID renderTextureTarget_{}; // 描画中の出力Texture
		uint64_t frameIndex_ = 0; // 転送を区別するframe番号
		FrameConstantBufferAllocator allocator_{}; // frameごとの転送領域

		//--------- functions ----------------------------------------------------

		// パイプラインのReflectionレイアウトを取得する
		const MaterialParameterLayout& ResolveLayout(const PipelineState& pipeline);
		// 変更検知済みのキャッシュエントリを取得する
		CachedBindingData& ResolveCacheEntry(const PipelineState& pipeline,
			const MaterialAsset& material,
			const MaterialParameterSet* overrides);

		// 既定値と上書きの転送を共通処理で行う
		D3D12_GPU_VIRTUAL_ADDRESS ResolveAndUploadParameters(
			GraphicsResourceRetirement& retirement, ID3D12Device* device, const PipelineState& pipeline,
			const MaterialAsset& material, const MaterialParameterSet* overrides,
			const MaterialParameterBufferBuilder::TextureResolver& resolveTexture);
	};
} // Engine
