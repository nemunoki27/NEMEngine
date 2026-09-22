#include "ShaderGraphNodePreviewUtility.h"

//============================================================================
//	include
//============================================================================


// c++
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <imgui_node_editor.h>


namespace Engine::ShaderGraphNodePreviewUtility {

	uint64_t CalculatePreviewHash(
		const Engine::ShaderGraphAsset& graph) {

		uint64_t hash = 14695981039346656037ull;
		for (const Engine::ShaderGraphParameter& parameter :
			graph.parameters) {

			hash = CombinePreviewHash(hash, parameter.id.value);
			hash = CombinePreviewHash(
				hash, static_cast<uint64_t>(parameter.type));
			hash = CombinePreviewHash(
				hash, HashPreviewValue(parameter.defaultValue));
		}
		for (const Engine::ShaderGraphNode& node : graph.nodes) {

			hash = CombinePreviewHash(hash, node.id.value);
			hash = CombinePreviewHash(
				hash, static_cast<uint64_t>(node.kind));
			hash = CombinePreviewHash(
				hash, node.parameterID.value);
			hash = CombinePreviewHash(
				hash, static_cast<uint64_t>(node.valueType));
			hash = CombinePreviewHash(
				hash, HashPreviewValue(node.value));
			hash = CombinePreviewHash(hash,
				static_cast<uint64_t>(node.sampler.filter));
			hash = CombinePreviewHash(hash,
				static_cast<uint64_t>(node.sampler.addressU));
			hash = CombinePreviewHash(hash,
				static_cast<uint64_t>(node.sampler.addressV));
			hash = CombinePreviewHash(hash,
				static_cast<uint64_t>(node.sampler.addressW));
			hash = CombinePreviewHash(hash,
				node.sampler.maxAnisotropy);
		}
		for (const Engine::ShaderGraphLink& link : graph.links) {

			hash = CombinePreviewHash(hash, link.outputNode.value);
			hash = CombinePreviewHash(hash, link.outputSlot);
			hash = CombinePreviewHash(hash, link.inputNode.value);
			hash = CombinePreviewHash(hash, link.inputSlot);
		}
		return hash;
	}

	const Engine::ShaderGraphNode* FindPreviewNode(
		const Engine::ShaderGraphAsset& graph,
		Engine::UUID id) {

		const auto found = std::find_if(
			graph.nodes.begin(), graph.nodes.end(),
			[&](const Engine::ShaderGraphNode& node) {
				return node.id == id;
			});
		return found != graph.nodes.end() ?
			&(*found) : nullptr;
	}

	const Engine::ShaderGraphLink* FindPreviewInput(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t slot) {

		const auto found = std::find_if(
			graph.links.begin(), graph.links.end(),
			[&](const Engine::ShaderGraphLink& link) {
				return link.inputNode == node.id &&
					link.inputSlot == slot;
			});
		return found != graph.links.end() ?
			&(*found) : nullptr;
	}

	Engine::ShaderGraphValueType ResolvePreviewOutputType(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t outputSlot,
		std::unordered_set<uint64_t>& visiting) {

		using NodeKind = Engine::ShaderGraphNodeKind;
		using ValueType = Engine::ShaderGraphValueType;
		if (!visiting.insert(node.id.value).second) {
			return ValueType::Invalid;
		}

		ValueType result = ValueType::Invalid;
		switch (node.kind) {
		case NodeKind::Parameter: {
			const Engine::ShaderGraphParameter* parameter =
				FindPreviewParameter(graph, node.parameterID);
			result = parameter ?
				parameter->type : node.valueType;
			break;
		}
		case NodeKind::Constant:
			result = node.valueType;
			break;
		case NodeKind::UV:
		case NodeKind::TilingAndOffset:
		case NodeKind::PolarCoordinates:
			result = ValueType::Float2;
			break;
		case NodeKind::WorldNormal:
		case NodeKind::WorldPosition:
		case NodeKind::NormalUnpack:
			result = ValueType::Float3;
			break;
		case NodeKind::Time:
			result = ValueType::Float;
			break;
		case NodeKind::Add:
		case NodeKind::Subtract:
		case NodeKind::Multiply:
		case NodeKind::Divide:
		case NodeKind::Power:
		case NodeKind::Lerp: {
			const ValueType a =
				ResolvePreviewInputType(
					graph, node, 0,
					ValueType::Float, visiting);
			const ValueType b =
				ResolvePreviewInputType(
					graph, node, 1,
					ValueType::Float, visiting);
			result = PreviewComponentCount(a) >=
				PreviewComponentCount(b) ? a : b;
			break;
		}
		case NodeKind::OneMinus:
		case NodeKind::Dither:
		case NodeKind::Saturate:
		case NodeKind::Sine:
		case NodeKind::Remap:
			result = ResolvePreviewInputType(
				graph, node, 0,
				ValueType::Float, visiting);
			break;
		case NodeKind::Split:
			result = ValueType::Float;
			break;
		case NodeKind::Combine:
			result = outputSlot == 0 ?
				ValueType::Float4 :
				(outputSlot == 1 ?
					ValueType::Float3 :
					ValueType::Float2);
			break;
		case NodeKind::TextureSample:
		case NodeKind::SceneColor:
			result = outputSlot == 0 ?
				ValueType::Float4 :
				(outputSlot == 1 ?
					ValueType::Float3 :
					ValueType::Float);
			break;
		default:
			break;
		}
		visiting.erase(node.id.value);
		return result;
	}

	Engine::ShaderGraphValueType ResolvePreviewOutputType(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t outputSlot) {

		std::unordered_set<uint64_t> visiting{};
		return ResolvePreviewOutputType(graph, node, outputSlot, visiting);
	}

	PreviewSwizzle ResolvePreviewSwizzle(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& source,
		uint32_t outputSlot) {

		using NodeKind = Engine::ShaderGraphNodeKind;
		if (source.kind == NodeKind::TextureSample ||
			source.kind == NodeKind::SceneColor) {

			static constexpr std::array swizzles{
				PreviewSwizzle::Identity,
				PreviewSwizzle::RGB,
				PreviewSwizzle::R,
				PreviewSwizzle::G,
				PreviewSwizzle::B,
				PreviewSwizzle::A,
			};
			return outputSlot < swizzles.size() ?
				swizzles[outputSlot] :
				PreviewSwizzle::Identity;
		}
		if (source.kind == NodeKind::Split) {
			static constexpr std::array swizzles{
				PreviewSwizzle::R,
				PreviewSwizzle::G,
				PreviewSwizzle::B,
				PreviewSwizzle::A,
			};
			return outputSlot < swizzles.size() ?
				swizzles[outputSlot] :
				PreviewSwizzle::Identity;
		}
		if (source.kind == NodeKind::Combine) {
			return outputSlot == 1 ?
				PreviewSwizzle::RGB :
				(outputSlot == 2 ?
					PreviewSwizzle::RG :
					PreviewSwizzle::Identity);
		}

		switch (ResolvePreviewOutputType(
			graph, source, outputSlot)) {
		case Engine::ShaderGraphValueType::Float:
			return PreviewSwizzle::R;
		case Engine::ShaderGraphValueType::Float2:
			return PreviewSwizzle::RG;
		case Engine::ShaderGraphValueType::Float3:
			return PreviewSwizzle::RGB;
		default:
			return PreviewSwizzle::Identity;
		}
	}

	PreviewOperation GetPreviewOperation(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node) {

		using NodeKind = Engine::ShaderGraphNodeKind;
		switch (node.kind) {
		case NodeKind::Parameter: {
			const Engine::ShaderGraphParameter* parameter =
				FindPreviewParameter(graph, node.parameterID);
			return parameter &&
				parameter->type ==
				Engine::ShaderGraphValueType::Texture2D ?
				PreviewOperation::TextureValue :
				PreviewOperation::Value;
		}
		case NodeKind::Constant: return PreviewOperation::Value;
		case NodeKind::UV: return PreviewOperation::UV;
		case NodeKind::WorldNormal: return PreviewOperation::WorldNormal;
		case NodeKind::WorldPosition: return PreviewOperation::WorldPosition;
		case NodeKind::Time: return PreviewOperation::Time;
		case NodeKind::Add: return PreviewOperation::Add;
		case NodeKind::Subtract: return PreviewOperation::Subtract;
		case NodeKind::Multiply: return PreviewOperation::Multiply;
		case NodeKind::Divide: return PreviewOperation::Divide;
		case NodeKind::Power: return PreviewOperation::Power;
		case NodeKind::Lerp: return PreviewOperation::Lerp;
		case NodeKind::OneMinus: return PreviewOperation::OneMinus;
		case NodeKind::Saturate: return PreviewOperation::Saturate;
		case NodeKind::Sine: return PreviewOperation::Sine;
		case NodeKind::Remap: return PreviewOperation::Remap;
		case NodeKind::TilingAndOffset:
			return PreviewOperation::TilingAndOffset;
		case NodeKind::PolarCoordinates:
			return PreviewOperation::PolarCoordinates;
		case NodeKind::Split: return PreviewOperation::Split;
		case NodeKind::Combine: return PreviewOperation::Combine;
		case NodeKind::TextureSample:
			return PreviewOperation::TextureSample;
		case NodeKind::NormalUnpack:
			return PreviewOperation::NormalUnpack;
		default:
			return PreviewOperation::Value;
		}
	}

	bool IsPreviewableNode(
		Engine::ShaderGraphNodeKind kind) {

		return kind !=
			Engine::ShaderGraphNodeKind::SurfaceOutput &&
			kind !=
			Engine::ShaderGraphNodeKind::UnlitOutput &&
			kind !=
			Engine::ShaderGraphNodeKind::PostProcessOutput &&
			kind !=
			Engine::ShaderGraphNodeKind::VertexOutput &&
			kind !=
			Engine::ShaderGraphNodeKind::SamplerState;
	}

	std::string PreviewTextureName(Engine::UUID nodeID) {

		return "ShaderGraphPreview_" +
			Engine::ToString(nodeID);
	}

	Engine::Vector4 ToPreviewVector(
		const Engine::MaterialParameterValue& parameter,
		Engine::ShaderGraphValueType type) {

		Engine::Vector4 result{};
		std::visit([&](const auto& value) {
			using ValueType =
				std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<ValueType, float>) {
				result.x = value;
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Vector2>) {

				result.x = value.x;
				result.y = value.y;
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Vector3>) {

				result.x = value.x;
				result.y = value.y;
				result.z = value.z;
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Vector4>) {

				result = value;
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Color4>) {

				result = Engine::Vector4(
					value.r, value.g,
					value.b, value.a);
			} else if constexpr (
				std::is_same_v<ValueType, int32_t> ||
				std::is_same_v<ValueType, uint32_t>) {

				result.x = static_cast<float>(value);
			} else if constexpr (
				std::is_same_v<ValueType, bool>) {

				result.x = value ? 1.0f : 0.0f;
			}
			}, parameter.value);
		if (type == Engine::ShaderGraphValueType::Float4 ||
			type == Engine::ShaderGraphValueType::Color) {

			return result;
		}
		result.w = 1.0f;
		return result;
	}

	Engine::Vector4 PreviewInputDefault(
		Engine::ShaderGraphNodeKind kind,
		uint32_t slot) {

		using NodeKind = Engine::ShaderGraphNodeKind;
		if ((kind == NodeKind::Divide ||
			kind == NodeKind::Power) && slot == 1) {

			return Engine::Vector4(
				1.0f, 1.0f, 1.0f, 1.0f);
		}
		if (kind == NodeKind::Lerp && slot == 2) {
			return Engine::Vector4(
				0.5f, 0.5f, 0.5f, 0.5f);
		}
		if (kind == NodeKind::Remap && slot == 1) {
			return Engine::Vector4(
				-1.0f, 1.0f, 0.0f, 1.0f);
		}
		if (kind == NodeKind::Remap && slot == 2) {
			return Engine::Vector4(
				0.0f, 1.0f, 0.0f, 1.0f);
		}
		if (kind == NodeKind::TilingAndOffset &&
			slot == 1) {

			return Engine::Vector4(
				1.0f, 1.0f, 0.0f, 1.0f);
		}
		if (kind == NodeKind::PolarCoordinates &&
			slot == 1) {

			return Engine::Vector4(
				0.5f, 0.5f, 0.0f, 1.0f);
		}
		if (kind == NodeKind::PolarCoordinates &&
			2 <= slot) {

			return Engine::Vector4(
				1.0f, 1.0f, 1.0f, 1.0f);
		}
		if (kind == NodeKind::Combine && slot == 3) {
			return Engine::Vector4(
				1.0f, 1.0f, 1.0f, 1.0f);
		}
		if (kind == NodeKind::NormalUnpack) {
			return Engine::Vector4(
				0.5f, 0.5f, 1.0f, 1.0f);
		}
		return Engine::Vector4{};
	}

	PreviewTextureReference ResolvePreviewTextureReference(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node) {

		const Engine::ShaderGraphNode* textureNode = &node;
		if (node.kind ==
			Engine::ShaderGraphNodeKind::TextureSample) {

			const Engine::ShaderGraphLink* link =
				FindPreviewInput(graph, node, 0);
			textureNode = link ?
				FindPreviewNode(
					graph, link->outputNode) :
				nullptr;
		}
		if (!textureNode ||
			textureNode->kind !=
			Engine::ShaderGraphNodeKind::Parameter) {

			return {};
		}

		const Engine::ShaderGraphParameter* parameter =
			FindPreviewParameter(
				graph, textureNode->parameterID);
		if (!parameter ||
			parameter->type !=
			Engine::ShaderGraphValueType::Texture2D) {

			return {};
		}

		const Engine::AssetID* assetID =
			std::get_if<Engine::AssetID>(
				&parameter->defaultValue.value);
		if (!assetID || !(*assetID)) {
			return {};
		}

		const bool sRGB =
			parameter->semantic ==
			Engine::MaterialParameterSemantic::BaseColorTexture ||
			parameter->semantic ==
			Engine::MaterialParameterSemantic::EmissiveTexture;
		return PreviewTextureReference{
			.assetID = *assetID,
			.sRGB = sRGB,
		};
	}

	uint64_t CombinePreviewHash(
		uint64_t seed, uint64_t value) {

		return seed ^ (value + 0x9e3779b97f4a7c15ull +
			(seed << 6) + (seed >> 2));
	}

	uint64_t HashPreviewValue(
		const Engine::MaterialParameterValue& parameter) {

		uint64_t hash = parameter.value.index();
		const auto appendFloat = [&](float value) {
			hash = CombinePreviewHash(
				hash, std::bit_cast<uint32_t>(value));
			};
		std::visit([&](const auto& value) {
			using ValueType =
				std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float>) {
				appendFloat(value);
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Vector2>) {

				appendFloat(value.x);
				appendFloat(value.y);
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Vector3>) {

				appendFloat(value.x);
				appendFloat(value.y);
				appendFloat(value.z);
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Vector4>) {

				appendFloat(value.x);
				appendFloat(value.y);
				appendFloat(value.z);
				appendFloat(value.w);
			} else if constexpr (
				std::is_same_v<ValueType, Engine::Color4>) {

				appendFloat(value.r);
				appendFloat(value.g);
				appendFloat(value.b);
				appendFloat(value.a);
			} else if constexpr (
				std::is_same_v<ValueType, Engine::AssetID>) {

				hash = CombinePreviewHash(hash, value.high);
				hash = CombinePreviewHash(hash, value.low);
			} else {
				hash = CombinePreviewHash(
					hash, static_cast<uint64_t>(value));
			}
			}, parameter.value);
		return hash;
	}

	uint32_t PreviewComponentCount(
		Engine::ShaderGraphValueType type) {

		switch (type) {
		case Engine::ShaderGraphValueType::Float: return 1;
		case Engine::ShaderGraphValueType::Float2: return 2;
		case Engine::ShaderGraphValueType::Float3: return 3;
		case Engine::ShaderGraphValueType::Float4:
		case Engine::ShaderGraphValueType::Color: return 4;
		default: return 0;
		}
	}

	Engine::ShaderGraphValueType ResolvePreviewInputType(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t inputSlot,
		Engine::ShaderGraphValueType fallback,
		std::unordered_set<uint64_t>& visiting) {

		const Engine::ShaderGraphLink* link =
			FindPreviewInput(graph, node, inputSlot);
		if (!link) {
			return fallback;
		}
		const Engine::ShaderGraphNode* source =
			FindPreviewNode(graph, link->outputNode);
		return source ?
			ResolvePreviewOutputType(
				graph, *source,
				link->outputSlot, visiting) :
			fallback;
	}
	std::vector<const Engine::ShaderGraphNode*>
		BuildPreviewOrder(
			const Engine::ShaderGraphAsset& graph) {

		std::vector<const Engine::ShaderGraphNode*> result{};
		std::unordered_map<uint64_t, uint8_t> states{};
		std::function<void(const Engine::ShaderGraphNode&)>
			visit = [&](const Engine::ShaderGraphNode& node) {

			uint8_t& state = states[node.id.value];
			if (state == 2 || state == 1) {
				return;
			}
			state = 1;
			for (const Engine::ShaderGraphLink& link :
				graph.links) {

				if (link.inputNode != node.id) {
					continue;
				}
				const Engine::ShaderGraphNode* source =
					FindPreviewNode(
						graph, link.outputNode);
				if (source &&
					IsPreviewableNode(source->kind)) {

					visit(*source);
				}
			}
			state = 2;
			if (IsPreviewableNode(node.kind)) {
				result.emplace_back(&node);
			}
			};

		result.reserve(graph.nodes.size());
		for (const Engine::ShaderGraphNode& node :
			graph.nodes) {

			if (IsPreviewableNode(node.kind) &&
				node.previewExpanded) {

				visit(node);
			}
		}
		return result;
	}
	const Engine::ShaderGraphParameter*
		FindPreviewParameter(
			const Engine::ShaderGraphAsset& graph,
			Engine::UUID id) {

		const auto found = std::find_if(
			graph.parameters.begin(), graph.parameters.end(),
			[&](const Engine::ShaderGraphParameter& parameter) {
				return parameter.id == id;
			});
		return found != graph.parameters.end() ?
			&(*found) : nullptr;
	}
}
