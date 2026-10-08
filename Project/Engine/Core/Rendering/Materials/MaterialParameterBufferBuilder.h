#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace Engine {

	//============================================================================
	//	MaterialParameterBufferBuilder class
	//	Materialの値をGPUの宣言配置へ詰めるクラス
	//============================================================================
	class MaterialParameterBufferBuilder {
	public:
		// Texture番号と代替値のCache可否
		struct TextureResolveResult {

			uint32_t index = kNoTextureIndex; // 解決したSRV番号
			bool cacheable = true; // 次のframeも再利用できるか
		};
		//========================================================================
		//	public Methods
		//========================================================================

		MaterialParameterBufferBuilder() = default;
		~MaterialParameterBufferBuilder() = default;

		// テクスチャSemanticとAssetIDからbindless SRV indexを解決する
		using TextureResolver = std::function<TextureResolveResult(MaterialParameterSemantic, const AssetID&)>;

		// MaterialAssetの値を指定レイアウトのbyte列に変換する
		static std::vector<uint8_t> Build(const MaterialAsset& material,
			const MaterialParameterLayout& layout,
			const TextureResolver& resolveTexture = {},
			bool* outTextureValuesCacheable = nullptr);
		// 呼び出し側が確保済みの領域へMaterialAssetの値を詰める
		static bool BuildInto(std::span<uint8_t> bytes,
			const MaterialAsset& material, const MaterialParameterLayout& layout,
			const TextureResolver& resolveTexture = {},
			bool* outTextureValuesCacheable = nullptr);

		// 上書きとTexture番号を解決して1要素分を詰める
		static std::vector<uint8_t> BuildElement(const MaterialParameterSet& defaults,
			const MaterialParameterSet& overrides,
			const MaterialParameterLayout& layout,
			const TextureResolver& resolveTexture,
			bool* outTextureValuesCacheable = nullptr);
		// 呼び出し側が確保済みの領域へ既定値と上書きを詰める
		static bool BuildElementInto(std::span<uint8_t> bytes,
			const MaterialParameterSet& defaults,
			const MaterialParameterSet& overrides,
			const MaterialParameterLayout& layout,
			const TextureResolver& resolveTexture,
			bool* outTextureValuesCacheable = nullptr);

		// キャッシュ変更検知用の順序非依存ハッシュを計算する
		static uint64_t ComputeHash(const MaterialParameterSet& parameters);
	};
} // Engine
