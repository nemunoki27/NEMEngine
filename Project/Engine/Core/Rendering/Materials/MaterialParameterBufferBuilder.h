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
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	MaterialParameterBufferBuilder class
	// MaterialAsset.parametersをHLSL側のCBV offsetに合わせて詰める補助クラス
	// reflectionのoffset/型に追従するため、シェーダーのcbufferパッキングに自動で合う
	//============================================================================
	class MaterialParameterBufferBuilder {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MaterialParameterBufferBuilder() = default;
		~MaterialParameterBufferBuilder() = default;

		// テクスチャ系paramのAssetIDをbindless SRV indexへ解決するコールバックで名前からsRGB可否を判定できる
		using TextureResolver = std::function<uint32_t(const std::string&, const AssetID&)>;

		// MaterialAssetの値を指定レイアウトのbyte列に変換する
		static std::vector<uint8_t> Build(const MaterialAsset& material, const MaterialParameterLayout& layout);
		// 呼び出し側が確保済みの領域へMaterialAssetの値を詰める
		static bool BuildInto(std::span<uint8_t> bytes,
			const MaterialAsset& material, const MaterialParameterLayout& layout);

		// マテリアル既定値にサブメッシュ上書きを重ね、テクスチャはbindless indexへ解決して1要素分を詰める
		static std::vector<uint8_t> BuildElement(
			const std::unordered_map<std::string, MaterialParameterValue>& defaults,
			const std::unordered_map<std::string, MaterialParameterValue>& overrides,
			const MaterialParameterLayout& layout,
			const TextureResolver& resolveTexture);
		// 呼び出し側が確保済みの領域へ既定値と上書きを詰める
		static bool BuildElementInto(std::span<uint8_t> bytes,
			const std::unordered_map<std::string, MaterialParameterValue>& defaults,
			const std::unordered_map<std::string, MaterialParameterValue>& overrides,
			const MaterialParameterLayout& layout,
			const TextureResolver& resolveTexture);

		// キャッシュ変更検知用の順序非依存ハッシュを計算する
		static uint64_t ComputeHash(
			const std::unordered_map<std::string, MaterialParameterValue>& parameters);
	};
} // Engine
