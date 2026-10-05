#include "ShaderGraphPreviewFingerprint.h"

//============================================================================
//	include
//============================================================================
// c++
#include <bit>
#include <type_traits>
#include <variant>

namespace Engine::ShaderGraphNodePreviewUtility {

	// 値と接続からプレビューの更新を判定する
	uint64_t CalculatePreviewHash(const Engine::ShaderGraphAsset& graph) {

		uint64_t hash = 14695981039346656037ull;
		for (const Engine::ShaderGraphParameter& parameter : graph.parameters) {

			hash = CombinePreviewHash(hash, parameter.id.value);
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(parameter.type));
			hash = CombinePreviewHash(hash, HashPreviewValue(parameter.defaultValue));
		}
		for (const Engine::ShaderGraphNode& node : graph.nodes) {

			hash = CombinePreviewHash(hash, node.id.value);
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.kind));
			hash = CombinePreviewHash(hash, node.parameterID.value);
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.valueType));
			hash = CombinePreviewHash(hash, HashPreviewValue(node.value));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.filter));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.addressU));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.addressV));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.addressW));
			hash = CombinePreviewHash(hash, node.sampler.maxAnisotropy);
		}
		for (const Engine::ShaderGraphLink& link : graph.links) {

			hash = CombinePreviewHash(hash, link.outputNode.value);
			hash = CombinePreviewHash(hash, link.outputSlot);
			hash = CombinePreviewHash(hash, link.inputNode.value);
			hash = CombinePreviewHash(hash, link.inputSlot);
		}
		return hash;
	}

	// 更新判定用のハッシュへ値を加える
	uint64_t CombinePreviewHash(uint64_t seed, uint64_t value) {

		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
	}

	// 設定値の型と内容からハッシュを作る
	uint64_t HashPreviewValue(const Engine::MaterialParameterValue& parameter) {

		uint64_t hash = parameter.value.index();
		const auto appendFloat = [&](float value) { hash = CombinePreviewHash(hash, std::bit_cast<uint32_t>(value)); };
		std::visit(
			[&](const auto& value) {
				using ValueType = std::decay_t<decltype(value)>;

				if constexpr (std::is_same_v<ValueType, float>) {
					appendFloat(value);
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {

					appendFloat(value.x);
					appendFloat(value.y);
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {

					appendFloat(value.x);
					appendFloat(value.y);
					appendFloat(value.z);
				} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {

					appendFloat(value.x);
					appendFloat(value.y);
					appendFloat(value.z);
					appendFloat(value.w);
				} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {

					appendFloat(value.r);
					appendFloat(value.g);
					appendFloat(value.b);
					appendFloat(value.a);
				} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {

					hash = CombinePreviewHash(hash, value.high);
					hash = CombinePreviewHash(hash, value.low);
				} else {
					hash = CombinePreviewHash(hash, static_cast<uint64_t>(value));
				}
			},
			parameter.value);
		return hash;
	}
} // Engine::ShaderGraphNodePreviewUtility
