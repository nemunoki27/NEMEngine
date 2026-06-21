#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <cstdint>
#include <functional>
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

		// マテリアル既定値にサブメッシュ上書きを重ね、テクスチャはbindless indexへ解決して1要素分を詰める
		static std::vector<uint8_t> BuildElement(
			const std::unordered_map<std::string, MaterialParameterValue>& defaults,
			const std::unordered_map<std::string, MaterialParameterValue>& overrides,
			const MaterialParameterLayout& layout,
			const TextureResolver& resolveTexture);
	};
} // Engine
