#include "ShaderGraphEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

// imgui
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_node_editor.h>

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

namespace {

	namespace ed = ax::NodeEditor;

	constexpr const char* kCreateNodePopup = "ShaderGraphCreateNode";
	constexpr const char* kNodeContextPopup = "ShaderGraphNodeContext";
	constexpr float kNodePinColumnGap = 32.0f;
	constexpr float kGroupNameEditWidth = 200.0f;
	constexpr float kGroupHorizontalPadding = 40.0f;
	constexpr float kGroupTopPadding = 56.0f;
	constexpr float kGroupBottomPadding = 40.0f;
	constexpr float kDuplicateOffset = 32.0f;
	constexpr float kDuplicateGroupSpacing = 48.0f;
	constexpr float kDefaultNodeMinimumWidth = 180.0f;
	constexpr float kDefaultNodePreviewDisplaySize = 144.0f;
	constexpr int32_t kDefaultNodePreviewTextureSize = 128;
	constexpr uint32_t kNodePreviewRTVReserve = 16;

	enum class PreviewOperation : uint32_t {

		Value = 0,
		UV,
		WorldNormal,
		WorldPosition,
		Add,
		Subtract,
		Multiply,
		Divide,
		Power,
		Lerp,
		OneMinus,
		Saturate,
		Sine,
		Remap,
		TilingAndOffset,
		PolarCoordinates,
		Split,
		Combine,
		TextureSample,
		NormalUnpack,
		TextureValue,
		Time,
	};

	enum class PreviewSwizzle : uint32_t {

		Identity = 0,
		RGB,
		R,
		G,
		B,
		A,
		RG,
	};

	struct PreviewConstants {

		uint32_t operation = 0;
		uint32_t connectedMask = 0;
		uint32_t textureIndex = UINT32_MAX;
		uint32_t outputValueType = 0;

		Engine::Vector4 literalValue{};
		std::array<Engine::Vector4, 4> inputDefaults{};
		std::array<uint32_t, 4> inputSwizzles{};
		Engine::Vector4 timeValues{};
	};

	struct PreviewTextureReference {

		Engine::AssetID assetID{};
		bool sRGB = false;
	};

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

	std::string MakeShaderGraphCompileState(
		const Engine::ShaderGraphAsset& graph) {

		nlohmann::json data = Engine::ToJson(graph);
		data.erase("groups");
		if (data.contains("nodes") && data["nodes"].is_array()) {
			for (nlohmann::json& node : data["nodes"]) {
				node.erase("groupID");
				node.erase("position");
				node.erase("previewExpanded");
			}
		}
		return data.dump();
	}

	bool HasCurrentGraphDefaults(
		const Engine::MaterialAsset& material,
		const Engine::MaterialAsset& expected) {

		if (material.shaderGraph != expected.shaderGraph) {
			return false;
		}
		const std::span<const Engine::MaterialParameterRecord> records =
			material.parameters.GetRecords();
		const std::span<const Engine::MaterialParameterRecord> expectedRecords =
			expected.parameters.GetRecords();
		if (records.size() != expectedRecords.size()) {
			return false;
		}
		for (size_t index = 0; index < records.size(); ++index) {
			if (records[index].id != expectedRecords[index].id ||
				records[index].namedValue.first !=
					expectedRecords[index].namedValue.first ||
				records[index].semantic != expectedRecords[index].semantic ||
				Engine::SerializeMaterialParameterValue(
					records[index].namedValue.second) !=
				Engine::SerializeMaterialParameterValue(
					expectedRecords[index].namedValue.second)) {

				return false;
			}
		}
		return true;
	}

	template <typename T>
	Engine::ValueEditResult D3D12EnumCombo(
		const char* label, T& currentValue) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(width <= 1.0f ? 1.0f : width);
		int32_t currentIndex = static_cast<int32_t>(
			Engine::EnumAdapter<T>::GetIndex(currentValue));
		const auto itemGetter = [](
			void*, int32_t index) -> const char* {

			constexpr auto names = magic_enum::enum_names<T>();
			constexpr std::string_view typeName =
				magic_enum::enum_type_name<T>();
			constexpr size_t separator = typeName.rfind('_');
			constexpr std::string_view prefix =
				separator == std::string_view::npos ?
				std::string_view{} :
				typeName.substr(0, separator + 1);
			if (index < 0 || names.size() <= static_cast<size_t>(index)) {
				return "";
			}
			std::string_view name = names[static_cast<size_t>(index)];
			if (!prefix.empty() && name.starts_with(prefix)) {
				name.remove_prefix(prefix.size());
			}
			return name.data();
		};
		result.valueChanged = ImGui::Combo(
			"##Value", &currentIndex, itemGetter, nullptr,
			static_cast<int32_t>(Engine::EnumAdapter<T>::GetEnumCount()));
		if (result.valueChanged) {
			currentValue = Engine::EnumAdapter<T>::GetValue(
				static_cast<uint32_t>(currentIndex));
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged ||
			ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
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

	Engine::ShaderGraphValueType ResolvePreviewOutputType(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t outputSlot,
		std::unordered_set<uint64_t>& visiting);

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
		uint32_t outputSlot = 0) {

		std::unordered_set<uint64_t> visiting{};
		return ResolvePreviewOutputType(
			graph, node, outputSlot, visiting);
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

	ImVec4 ToImVec4(const Engine::Color4& color) {

		return ImVec4(color.r, color.g, color.b, color.a);
	}

	Engine::Color4 ToColor4(const ImVec4& color) {

		return Engine::Color4(
			color.x, color.y, color.z, color.w);
	}

	Engine::Color4 ReadColor(
		const nlohmann::json& data,
		const char* name,
		const Engine::Color4& fallback) {

		const auto found = data.find(name);
		if (found == data.end() ||
			!found->is_object()) {
			return fallback;
		}
		return Engine::Color4(
			found->value("r", fallback.r),
			found->value("g", fallback.g),
			found->value("b", fallback.b),
			found->value("a", fallback.a));
	}

	Engine::Vector4 ReadVector4(
		const nlohmann::json& data,
		const char* name,
		const Engine::Vector4& fallback) {

		const auto found = data.find(name);
		if (found == data.end() ||
			!found->is_object()) {
			return fallback;
		}
		return Engine::Vector4(
			found->value("x", fallback.x),
			found->value("y", fallback.y),
			found->value("z", fallback.z),
			found->value("w", fallback.w));
	}

	Engine::Vector2 ReadVector2(
		const nlohmann::json& data,
		const char* name,
		const Engine::Vector2& fallback) {

		const auto found = data.find(name);
		if (found == data.end() ||
			!found->is_object()) {
			return fallback;
		}
		return Engine::Vector2(
			found->value("x", fallback.x),
			found->value("y", fallback.y));
	}

	Engine::FloatEditSetting AppearanceFloatSetting(
		float minValue, float maxValue,
		float dragSpeed = 0.1f) {

		return Engine::FloatEditSetting{
			.dragSpeed = dragSpeed,
			.minValue = minValue,
			.maxValue = maxValue,
		};
	}

	Engine::PropertyRowSetting NodeValueRowSetting(
		const char* label, float rowWidth) {

		return Engine::PropertyRowSetting{
			.labelWidth =
				ImGui::CalcTextSize(label).x + 4.0f,
			.rowWidth = rowWidth,
		};
	}

	Engine::ValueEditResult DrawNodeColorEdit(
		const char* label, Engine::Color4& value,
		float rowWidth) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(
			label, NodeValueRowSetting(
				label, rowWidth))) {
			return result;
		}

		float color[4] = {
			value.r, value.g, value.b, value.a
		};
		ImGui::SetNextItemWidth(
			ImGui::GetContentRegionAvail().x);
		result.valueChanged = ImGui::ColorEdit4(
			"##Value", color,
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_NoInputs);
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished =
			ImGui::IsItemDeactivatedAfterEdit() ||
			result.valueChanged;

		if (result.valueChanged) {
			value.r = color[0];
			value.g = color[1];
			value.b = color[2];
			value.a = color[3];
		}
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	uintptr_t ToNodeEditorID(Engine::UUID id) {

		return static_cast<uintptr_t>(id.value);
	}

	uintptr_t MakePinID(
		Engine::UUID node, bool input, uint32_t slot) {

		uint64_t value = node.value;
		value ^= input ?
			0x9e3779b97f4a7c15ull :
			0xc2b2ae3d27d4eb4full;
		value ^= static_cast<uint64_t>(slot + 1u) *
			0x165667b19e3779f9ull;
		return static_cast<uintptr_t>(value != 0 ? value : 1);
	}

	std::string GraphFileStem(
		const std::filesystem::path& graphPath) {

		return Engine::Algorithm::PathToUTF8(
			graphPath.stem().stem());
	}

	bool WriteTextFile(
		const std::filesystem::path& path,
		std::string_view source) {

		std::error_code ec{};
		std::filesystem::create_directories(
			path.parent_path(), ec);
		if (ec) {
			return false;
		}
		std::ofstream stream(
			path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			return false;
		}
		stream.write(
			source.data(),
			static_cast<std::streamsize>(source.size()));
		return stream.good();
	}

	nlohmann::json BuildShaderAssetJson(
		std::string_view name,
		Engine::AssetID pixelShader,
		std::string_view pixelEntry,
		std::span<const Engine::ShaderParameterMetadata>
			parameters) {

		nlohmann::json colorParameterNames =
			nlohmann::json::array();
		nlohmann::json parameterMetadata =
			nlohmann::json::array();
		for (const Engine::ShaderParameterMetadata&
			parameter : parameters) {

			if (parameter.isColor) {
				colorParameterNames.push_back(
					parameter.shaderName);
			}
			parameterMetadata.push_back({
				{ "shaderName", parameter.shaderName },
				{ "displayName", parameter.displayName },
				{ "id", Engine::ToString(
					Engine::UUID{
						parameter.id.value }) },
				{ "semantic",
					Engine::EnumAdapter<
						Engine::MaterialParameterSemantic>::
					ToString(parameter.semantic) },
				{ "isColor", parameter.isColor },
				});
		}
		nlohmann::json data{
			{ "name", name },
			{ "stages", nlohmann::json::array({
				{
					{ "stage", "PS" },
					{ "file", Engine::ToAssetReferenceJson(pixelShader) },
					{ "entry", pixelEntry },
					{ "profile", "ps_6_6" },
				},
				}) },
			{ "colorParameters", std::move(colorParameterNames) },
			{ "parameters", std::move(parameterMetadata) },
		};
		return data;
	}

	Engine::MaterialAsset CreateShaderGraphMaterial(
		std::string_view name,
		Engine::ShaderGraphTarget target) {

		using namespace Engine;

		if (target == ShaderGraphTarget::Mesh) {
			return CreateDefaultMeshMaterialAsset(name);
		}

		MaterialAsset material{};
		material.name =
			name.empty() ? "NewMaterial" : std::string(name);
		material.domain =
			target == ShaderGraphTarget::Sprite ||
			target == ShaderGraphTarget::Text ||
			target == ShaderGraphTarget::Primitive2D ?
			MaterialDomain::UI : MaterialDomain::Surface;
		material.usage =
			target == ShaderGraphTarget::Sprite ? MaterialUsage::Sprite :
			(target == ShaderGraphTarget::Text ? MaterialUsage::Text :
				(target == ShaderGraphTarget::FillMesh ?
					MaterialUsage::FillFaceMesh :
					((target == ShaderGraphTarget::Particle ||
						target == ShaderGraphTarget::Trail) ?
						MaterialUsage::Particle : MaterialUsage::Generic)));

		auto addPass = [&](MaterialPassKind passKind,
			AssetID pipeline,
			PipelineVariantKind variant) {

			material.passes.emplace_back(MaterialPassBinding{
				.passKind = passKind,
				.pipeline = pipeline,
				.preferredVariant = variant,
				});
		};

		switch (target) {
		case ShaderGraphTarget::Primitive3D:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultPrimitive,
				PipelineVariantKind::GraphicsMesh);
			addPass(
				MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::DefaultPrimitiveTransparent,
				PipelineVariantKind::GraphicsMesh);
			break;
		case ShaderGraphTarget::FillMesh:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultFillMesh,
				PipelineVariantKind::GraphicsVertex);
			addPass(
				MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::DefaultFillMeshTransparent,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Sprite:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultSprite,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Text:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultText,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Primitive2D:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultPrimitive2D,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Particle:
			addPass(MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::DefaultParticle,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Trail:
			addPass(MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::ParticleTrail,
				PipelineVariantKind::GraphicsMesh);
			break;
		case ShaderGraphTarget::Mesh:
			break;
		}
		return material;
	}

	Engine::MaterialParameterValue DefaultValueForGraphType(
		Engine::ShaderGraphValueType type) {

		Engine::MaterialParameterValue value{};
		switch (type) {
		case Engine::ShaderGraphValueType::Float:
			value.value = 0.0f;
			break;
		case Engine::ShaderGraphValueType::Float2:
			value.value = Engine::Vector2{};
			break;
		case Engine::ShaderGraphValueType::Float3:
			value.value = Engine::Vector3{};
			break;
		case Engine::ShaderGraphValueType::Float4:
			value.value = Engine::Vector4{};
			break;
		case Engine::ShaderGraphValueType::Color:
			value.value =
				Engine::Color4(1.0f, 1.0f, 1.0f, 1.0f);
			break;
		case Engine::ShaderGraphValueType::Texture2D:
			value.value = Engine::AssetID{};
			break;
		case Engine::ShaderGraphValueType::Boolean:
			value.value = false;
			break;
		case Engine::ShaderGraphValueType::Integer:
			value.value = int32_t{};
			break;
		default:
			value.value = 0.0f;
			break;
		}
		return value;
	}

	bool ReadRendererMaterial(
		Engine::ECSWorld& world,
		const Engine::Entity& entity,
		Engine::ShaderGraphTarget target,
		Engine::AssetID& outMaterial) {

		switch (target) {
		case Engine::ShaderGraphTarget::Mesh:
			if (const auto* renderer =
				world.TryGetComponent<
					Engine::MeshRendererComponent>(entity)) {

				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive3D:
			if (const auto* renderer =
				world.TryGetComponent<
					Engine::PrimitiveRendererComponent>(entity)) {

				if (Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive2D:
			if (const auto* renderer =
				world.TryGetComponent<
					Engine::PrimitiveRendererComponent>(entity)) {

				if (!Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::FillMesh:
			if (const auto* renderer =
				world.TryGetComponent<
					Engine::FillMeshRendererComponent>(entity)) {

				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Sprite:
			if (const auto* renderer =
				world.TryGetComponent<
					Engine::SpriteRendererComponent>(entity)) {

				outMaterial = renderer->material;
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Text:
			if (const auto* renderer =
				world.TryGetComponent<
					Engine::TextRendererComponent>(entity)) {

				outMaterial = renderer->material;
				return true;
			}
			break;
		}
		return false;
	}

	bool WriteRendererMaterial(
		Engine::ECSWorld& world,
		const Engine::Entity& entity,
		Engine::ShaderGraphTarget target,
		Engine::AssetID material) {

		switch (target) {
		case Engine::ShaderGraphTarget::Mesh:
			if (auto* renderer =
				world.TryGetComponent<
					Engine::MeshRendererComponent>(entity)) {

				renderer->material = material;
				world.MarkComponentModified<
					Engine::MeshRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive3D:
			if (auto* renderer =
				world.TryGetComponent<
					Engine::PrimitiveRendererComponent>(entity)) {

				if (Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				renderer->material = material;
				world.MarkComponentModified<
					Engine::PrimitiveRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Primitive2D:
			if (auto* renderer =
				world.TryGetComponent<
					Engine::PrimitiveRendererComponent>(entity)) {

				if (!Engine::IsPrimitiveScreen2D(*renderer)) {
					return false;
				}
				renderer->material = material;
				world.MarkComponentModified<
					Engine::PrimitiveRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::FillMesh:
			if (auto* renderer =
				world.TryGetComponent<
					Engine::FillMeshRendererComponent>(entity)) {

				renderer->material = material;
				world.MarkComponentModified<
					Engine::FillMeshRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Sprite:
			if (auto* renderer =
				world.TryGetComponent<
					Engine::SpriteRendererComponent>(entity)) {

				renderer->material = material;
				world.MarkComponentModified<
					Engine::SpriteRendererComponent>(entity);
				return true;
			}
			break;
		case Engine::ShaderGraphTarget::Text:
			if (auto* renderer =
				world.TryGetComponent<
					Engine::TextRendererComponent>(entity)) {

				renderer->material = material;
				world.MarkComponentModified<
					Engine::TextRendererComponent>(entity);
				return true;
			}
			break;
		}
		return false;
	}
}

//============================================================================
//	ShaderGraphEditorTool::PreviewState structure
//============================================================================
struct Engine::ShaderGraphEditorTool::PreviewState {

	PreviewState() {

		for (uint32_t index = 0;
			index < inputSlots.size(); ++index) {

			inputSlots[index] =
				bindCache.AddSlotByRegister(
					ShaderBindingKind::SRV,
					index, 0);
		}
		constantsSlot =
			bindCache.AddSlotByRegister(
				ShaderBindingKind::CBV, 0, 0);
	}

	PipelineState pipeline{};
	PipelineBindingCache bindCache{};
	bool pipelineInitialized = false;
	bool pipelineAttempted = false;

	std::array<PipelineBindingCache::SlotID, 4>
		inputSlots{};
	PipelineBindingCache::SlotID constantsSlot =
		PipelineBindingCache::kInvalidSlot;

	std::array<std::vector<
		std::unique_ptr<DxConstBuffer<PreviewConstants>>>,
		kGraphicsFrameContextCount> constantBuffers{};
	std::array<uint32_t, kGraphicsFrameContextCount>
		constantBufferIndices{};
	std::array<uint64_t, kGraphicsFrameContextCount>
		constantBufferFrameSerials{};

	std::unordered_set<std::string> textureNames{};
	std::vector<std::pair<std::string, uint64_t>>
		retiredTextures{};
	std::unordered_map<uint64_t, uint32_t>
		textureIndices{};
	uint64_t graphHash = 0;
	bool previewsValid = false;
	bool descriptorLimitReached = false;

	DxConstBuffer<PreviewConstants>& AllocateConstantBuffer(
		GraphicsCore& graphicsCore) {

		const uint32_t frameIndex =
			GraphicsFrameState::GetCurrentIndex();
		const uint64_t frameSerial =
			GraphicsFrameState::GetFrameSerial();
		if (constantBufferFrameSerials[frameIndex] !=
			frameSerial) {

			constantBufferFrameSerials[frameIndex] =
				frameSerial;
			constantBufferIndices[frameIndex] = 0;
		}

		auto& buffers = constantBuffers[frameIndex];
		uint32_t& index =
			constantBufferIndices[frameIndex];
		if (buffers.size() <= index) {

			auto buffer = std::make_unique<
				DxConstBuffer<PreviewConstants>>();
			buffer->CreateBuffer(
				graphicsCore.GetDXObject().GetDevice());
			buffers.emplace_back(std::move(buffer));
		}
		return *buffers[index++];
	}
};

Engine::ShaderGraphEditorTool::ShaderGraphEditorTool() {

	RestoreDefaultAppearance();
	LoadAppearanceSettings();
	previewState_ = std::make_unique<PreviewState>();
}

Engine::ShaderGraphEditorTool::~ShaderGraphEditorTool() {

	ClearNodePreviews();
	ResetNodeEditor();
}

void Engine::ShaderGraphEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::OpenAsset(
	AssetID assetID) {

	pendingAsset_ = assetID;
	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::DrawEditorTool(
	const EditorToolContext& context) {

	commandPanelFocused_ = false;
	if (pendingAsset_) {
		RestorePreviewMaterial(context);
		LoadGraph(context, pendingAsset_);
		pendingAsset_ = {};
	}
	if (openWindow_) {
		DrawWindow(context);
	}
	if (!openWindow_) {
		RestorePreviewMaterial(context);
		return;
	}
	UpdateMaterialPreview(context);
}

void Engine::ShaderGraphEditorTool::DrawWindow(
	const EditorToolContext& context) {

	const bool visible = ImGui::Begin(
		"シェーダーグラフ", &openWindow_,
		ImGuiWindowFlags_MenuBar);
	commandPanelFocused_ =
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (!visible) {

		ImGui::End();
		return;
	}

	DrawToolbar(context);
	ImGui::Separator();

	const float panelWidth =
		(std::min)(360.0f, ImGui::GetContentRegionAvail().x * 0.35f);
	if (ImGui::BeginChild(
		"ShaderGraphParameters",
		ImVec2(panelWidth, 0.0f),
		ImGuiChildFlags_Borders |
		ImGuiChildFlags_ResizeX)) {

		DrawParameterPanel(context);
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if (ImGui::BeginChild(
		"ShaderGraphCanvas",
		ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders)) {

		DrawGraph(context);
	}
	ImGui::EndChild();
	if (graphLoaded_ &&
		!ImGui::IsAnyItemActive() &&
		!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {

		CommitGraphHistory();
	}
	ImGui::End();
}

void Engine::ShaderGraphEditorTool::DrawToolbar(
	const EditorToolContext& context) {

	AssetDatabase* assetDatabase =
		context.toolContext.assetDatabase;
	MyGUI::ScopedPropertyLabelWidth labelWidth(
		"ShaderGraphToolbar");
	AssetID selected = selectedAsset_;
	if (MyGUI::AssetReferenceField(
		"グラフ", selected, assetDatabase,
		{ AssetType::ShaderGraph }).valueChanged) {

		RestorePreviewMaterial(context);
		LoadGraph(context, selected);
	}

	MyGUI::EnumCombo("作成種類", createDomain_);
	if (createDomain_ == ShaderGraphDomain::Surface) {
		MyGUI::EnumCombo("作成対象", createTarget_);
	}
	MyGUI::InputText("作成先", createAssetPath_);
	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button("新規作成", ImVec2(buttonWidth, 0.0f))) {
		CreateGraph(context);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!graphLoaded_);
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		CaptureNodePositions();
		if (const AssetMeta* meta =
			assetDatabase ? assetDatabase->Find(selectedAsset_) : nullptr) {

			JsonAdapter::Save(
				assetDatabase->ResolveFullPath(meta->guid),
				ToJson(graph_));
			graphDirty_ = false;
			statusMessage_ = "保存しました";
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(
		"保存してコンパイル",
		ImVec2(buttonWidth, 0.0f))) {

		SaveAndCompile(context);
	}
	ImGui::EndDisabled();

	const float historyButtonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	ImGui::BeginDisabled(!graphLoaded_ || !history_.CanUndo());
	if (ImGui::Button(
		"元に戻す", ImVec2(historyButtonWidth, 0.0f))) {
		UndoGraph();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!graphLoaded_ || !history_.CanRedo());
	if (ImGui::Button(
		"やり直す", ImVec2(historyButtonWidth, 0.0f))) {
		RedoGraph();
	}
	ImGui::EndDisabled();

	if (!statusMessage_.empty()) {
		ImGui::TextUnformatted(statusMessage_.c_str());
	} else {
		ImGui::Dummy(ImVec2(
			0.0f, ImGui::GetTextLineHeight()));
	}
}

void Engine::ShaderGraphEditorTool::DrawParameterPanel(
	const EditorToolContext& context) {

	DrawAppearancePanel();
	if (!graphLoaded_) {
		return;
	}

	ImGui::Separator();
	DrawGraphSettings(context);

	if (graph_.domain == ShaderGraphDomain::Surface) {
		DrawPreviewSetting(context);
	}

	ImGui::SeparatorText("公開パラメータ");
	for (uint32_t index = 0;
		index < graph_.parameters.size(); ++index) {

		const ShaderGraphParameter& parameter =
			graph_.parameters[index];
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(
			parameter.name.c_str(),
			selectedParameter_ ==
				static_cast<int32_t>(index))) {

			selectedParameter_ =
				static_cast<int32_t>(index);
		}
		ImGui::PopID();
	}

	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button(
		"追加", ImVec2(buttonWidth, 0.0f))) {

		const uint32_t suffix =
			static_cast<uint32_t>(graph_.parameters.size() + 1);
		ShaderGraphParameter parameter{
			.id = UUID::New(),
			.name = "Parameter" + std::to_string(suffix),
			.type = ShaderGraphValueType::Float,
			.defaultValue =
				DefaultValueForGraphType(
					ShaderGraphValueType::Float),
		};
		graph_.parameters.emplace_back(
			std::move(parameter));
		selectedParameter_ =
			static_cast<int32_t>(
				graph_.parameters.size() - 1);
		graphDirty_ = true;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(
		selectedParameter_ < 0 ||
		static_cast<size_t>(selectedParameter_) >=
			graph_.parameters.size());
	if (ImGui::Button(
		"削除", ImVec2(buttonWidth, 0.0f))) {

		RemoveParameter(
			static_cast<uint32_t>(selectedParameter_));
	}
	ImGui::EndDisabled();

	if (0 <= selectedParameter_ &&
		static_cast<size_t>(selectedParameter_) <
			graph_.parameters.size()) {

		ImGui::SeparatorText("パラメータ設定");
		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"ShaderGraphParameterSettings");
		DrawParameterEditor(
			context,
			graph_.parameters[
				static_cast<size_t>(selectedParameter_)]);
	}

	DrawKeywordEditor();
	DrawSelectedNodeEditor(context);
	DrawDiagnostics();
}

void Engine::ShaderGraphEditorTool::DrawDiagnostics() {

	if (latestDiagnostics_.empty()) {
		return;
	}
	ImGui::SeparatorText("コンパイル診断");
	for (uint32_t index = 0;
		index < latestDiagnostics_.size(); ++index) {

		const ShaderGraphDiagnostic& diagnostic =
			latestDiagnostics_[index];
		const char* prefix =
			diagnostic.severity == ShaderGraphDiagnosticSeverity::Error ?
			"エラー" :
			(diagnostic.severity == ShaderGraphDiagnosticSeverity::Warning ?
				"警告" : "情報");
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(
			(std::string(prefix) + ": " + diagnostic.message).c_str())) {

			if (nodeEditor_ && diagnostic.node) {
				ed::SetCurrentEditor(nodeEditor_);
				ed::ClearSelection();
				ed::SelectNode(ed::NodeId(
					ToNodeEditorID(diagnostic.node)));
				ed::NavigateToSelection(true);
				ed::SetCurrentEditor(nullptr);
			}
		}
		ImGui::PopID();
	}
}

void Engine::ShaderGraphEditorTool::DrawGraphSettings(
	const EditorToolContext& context) {

	if (!MyGUI::CollapsingHeader("グラフ設定", true)) {
		return;
	}

	MyGUI::ScopedPropertyLabelWidth labelWidth(
		"ShaderGraphSettings");
	graphDirty_ |= MyGUI::InputText(
		"名前", graph_.name).valueChanged;
	ImGui::Text("種類: %s",
		EnumAdapter<ShaderGraphDomain>::ToString(graph_.domain));
	graphDirty_ |= MyGUI::EnumCombo(
		"既定精度", graph_.defaultPrecision).valueChanged;
	if (graph_.domain == ShaderGraphDomain::PostProcess) {
		return;
	}

	const ShaderGraphTarget oldTarget = graph_.target;
	if (MyGUI::EnumCombo(
		"描画対象", graph_.target).valueChanged) {

		RestorePreviewMaterial(context);
		const bool is3D =
			IsShaderGraph3DTarget(graph_.target);
		for (ShaderGraphNode& node : graph_.nodes) {
			if (node.id == graph_.outputNode) {
				node.kind = is3D ?
					ShaderGraphNodeKind::SurfaceOutput :
					ShaderGraphNodeKind::UnlitOutput;
				break;
			}
		}
		const uint32_t inputCount = is3D ? 8u : 3u;
		std::erase_if(
			graph_.links,
			[&](const ShaderGraphLink& link) {
				return link.inputNode == graph_.outputNode &&
					inputCount <= link.inputSlot;
		});
		if (oldTarget != graph_.target) {
			if (!SupportsShaderGraphVertexOutput(
				graph_.target) &&
				graph_.vertexOutputNode) {

				RemoveNode(graph_.vertexOutputNode);
				graph_.vertexOutputNode = UUID{};
			}
			graphDirty_ = true;
			ResetNodeEditor();
			restoreNodePositions_ = true;
		}
	}
	graphDirty_ |= MyGUI::EnumCombo(
		"サーフェス", graph_.surfaceMode).valueChanged;
	graphDirty_ |= MyGUI::EnumCombo(
		"ブレンド", graph_.renderState.blendMode).valueChanged;
	graphDirty_ |= MyGUI::Checkbox(
		"両面描画", graph_.renderState.twoSided);
	graphDirty_ |= D3D12EnumCombo(
		"塗りモード", graph_.renderState.fillMode).valueChanged;
	graphDirty_ |= D3D12EnumCombo(
		"カリング", graph_.renderState.cullMode).valueChanged;
	graphDirty_ |= MyGUI::Checkbox(
		"前面反時計回り", graph_.renderState.frontCounterClockwise);
	graphDirty_ |= MyGUI::Checkbox(
		"深度クリップ", graph_.renderState.depthClipEnable);
	graphDirty_ |= MyGUI::Checkbox(
		"深度書き込み", graph_.renderState.depthWrite);
	graphDirty_ |= MyGUI::Checkbox(
		"深度テスト", graph_.renderState.depthTest);
	graphDirty_ |= D3D12EnumCombo(
		"深度比較", graph_.renderState.depthFunc).valueChanged;
	graphDirty_ |= MyGUI::Checkbox(
		"ステンシル", graph_.renderState.stencilEnable);
	graphDirty_ |= MyGUI::Checkbox(
		"アルファクリップ", graph_.renderState.alphaClipping);
	if (IsShaderGraph3DTarget(graph_.target)) {
		graphDirty_ |= MyGUI::Checkbox(
			"影を落とす", graph_.renderState.castShadows);
		graphDirty_ |= MyGUI::Checkbox(
			"影を受ける", graph_.renderState.receiveShadows);
	}

}

void Engine::ShaderGraphEditorTool::DrawKeywordEditor() {

	ImGui::SeparatorText("キーワード");
	for (uint32_t index = 0; index < graph_.keywords.size(); ++index) {
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(
			graph_.keywords[index].name.c_str(),
			selectedKeyword_ == static_cast<int32_t>(index))) {
			selectedKeyword_ = static_cast<int32_t>(index);
		}
		ImGui::PopID();
	}

	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("追加##Keyword", ImVec2(buttonWidth, 0.0f))) {
		const uint32_t suffix =
			static_cast<uint32_t>(graph_.keywords.size() + 1);
		graph_.keywords.emplace_back(ShaderGraphKeyword{
			.id = UUID::New(),
			.name = "Keyword" + std::to_string(suffix),
			.referenceName = "KEYWORD_" + std::to_string(suffix),
			});
		selectedKeyword_ =
			static_cast<int32_t>(graph_.keywords.size() - 1);
		graphDirty_ = true;
	}
	ImGui::SameLine();
	const bool validSelection = selectedKeyword_ >= 0 &&
		static_cast<size_t>(selectedKeyword_) < graph_.keywords.size();
	ImGui::BeginDisabled(!validSelection);
	if (ImGui::Button("削除##Keyword", ImVec2(buttonWidth, 0.0f))) {
		const UUID keywordID = graph_.keywords[
			static_cast<size_t>(selectedKeyword_)].id;
		std::erase_if(graph_.nodes,
			[&](const ShaderGraphNode& node) {
				return node.kind == ShaderGraphNodeKind::Keyword &&
					node.keywordID == keywordID;
			});
		std::erase_if(graph_.links,
			[&](const ShaderGraphLink& link) {
				return std::none_of(
					graph_.nodes.begin(), graph_.nodes.end(),
					[&](const ShaderGraphNode& node) {
						return node.id == link.outputNode;
					});
			});
		graph_.keywords.erase(
			graph_.keywords.begin() + selectedKeyword_);
		selectedKeyword_ = -1;
		graphDirty_ = true;
	}
	ImGui::EndDisabled();
	if (!validSelection || selectedKeyword_ < 0) {
		return;
	}

	ShaderGraphKeyword& keyword = graph_.keywords[
		static_cast<size_t>(selectedKeyword_)];
	MyGUI::ScopedPropertyLabelWidth labelWidth(
		"ShaderGraphKeywordSettings");
	graphDirty_ |= MyGUI::InputText(
		"名前", keyword.name).valueChanged;
	graphDirty_ |= MyGUI::InputText(
		"参照名", keyword.referenceName).valueChanged;
	if (MyGUI::EnumCombo("型", keyword.type).valueChanged) {
		keyword.defaultIndex = 0;
		if (keyword.type == ShaderGraphKeywordType::Boolean) {
			keyword.entries.clear();
		}
		graphDirty_ = true;
	}
	graphDirty_ |= MyGUI::Checkbox(
		"実行時切り替え", keyword.runtimeToggle);
	if (ImGui::BeginItemTooltip()) {
		ImGui::TextUnformatted(keyword.runtimeToggle ?
			"Material Instanceから値を変更する動的分岐" :
			"既定値をHLSLへ埋め込み、保存時に再コンパイル");
		ImGui::EndTooltip();
	}
	if (keyword.type == ShaderGraphKeywordType::Boolean) {
		bool defaultValue = keyword.defaultIndex != 0;
		if (MyGUI::Checkbox("既定値", defaultValue)) {
			keyword.defaultIndex = defaultValue ? 1u : 0u;
			graphDirty_ = true;
		}
		return;
	}

	int32_t defaultIndex = static_cast<int32_t>(keyword.defaultIndex);
	if (MyGUI::DragInt("既定値", defaultIndex, {
		.dragSpeed = 1.0f,
		.minValue = 0,
		.maxValue = (std::max)(
			static_cast<int32_t>(keyword.entries.size()) - 1, 0),
		}).valueChanged) {
		keyword.defaultIndex = static_cast<uint32_t>(defaultIndex);
		graphDirty_ = true;
	}
	for (uint32_t index = 0; index < keyword.entries.size();) {
		ImGui::PushID(static_cast<int>(index));
		const std::string label = "値 " + std::to_string(index);
		graphDirty_ |= MyGUI::InputText(
			label.c_str(), keyword.entries[index]).valueChanged;
		ImGui::SameLine();
		if (ImGui::SmallButton("削除")) {
			keyword.entries.erase(keyword.entries.begin() + index);
			keyword.defaultIndex = (std::min)(
				keyword.defaultIndex,
				keyword.entries.empty() ? 0u :
					static_cast<uint32_t>(keyword.entries.size() - 1));
			graphDirty_ = true;
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++index;
	}
	if (ImGui::Button("列挙値を追加", ImVec2(-FLT_MIN, 0.0f))) {
		keyword.entries.emplace_back(
			"Value" + std::to_string(keyword.entries.size()));
		graphDirty_ = true;
	}
}

void Engine::ShaderGraphEditorTool::DrawSelectedNodeEditor(
	const EditorToolContext& context) {

	if (!nodeEditor_) {
		return;
	}
	ed::SetCurrentEditor(nodeEditor_);
	const std::vector<UUID> selected = GetSelectedGraphNodes();
	ed::SetCurrentEditor(nullptr);
	if (selected.size() != 1) {
		return;
	}

	const auto found = std::find_if(
		graph_.nodes.begin(), graph_.nodes.end(),
		[&](const ShaderGraphNode& node) {
			return node.id == selected.front();
		});
	if (found == graph_.nodes.end()) {
		return;
	}

	ShaderGraphNode& node = *found;
	ImGui::SeparatorText("選択ノード");
	ImGui::TextUnformatted(GetShaderGraphNodeName(node.kind).data());
	if (node.kind != ShaderGraphNodeKind::SamplerState) {
		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"ShaderGraphSelectedNode");
		graphDirty_ |= MyGUI::EnumCombo(
			"精度", node.precision).valueChanged;
	}
	if (node.kind == ShaderGraphNodeKind::SamplerState) {

		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"ShaderGraphSamplerState");
		graphDirty_ |= D3D12EnumCombo(
			"フィルタ", node.sampler.filter).valueChanged;
		graphDirty_ |= D3D12EnumCombo(
			"アドレスU", node.sampler.addressU).valueChanged;
		graphDirty_ |= D3D12EnumCombo(
			"アドレスV", node.sampler.addressV).valueChanged;
		graphDirty_ |= D3D12EnumCombo(
			"アドレスW", node.sampler.addressW).valueChanged;
		graphDirty_ |= D3D12EnumCombo(
			"比較関数", node.sampler.comparisonFunc).valueChanged;
		graphDirty_ |= D3D12EnumCombo(
			"境界色", node.sampler.borderColor).valueChanged;
		int32_t maxAnisotropy = static_cast<int32_t>(
			node.sampler.maxAnisotropy);
		if (MyGUI::DragInt("異方性", maxAnisotropy, {
			.dragSpeed = 1.0f,
			.minValue = 1,
			.maxValue = 16,
			}).valueChanged) {
			node.sampler.maxAnisotropy =
				static_cast<uint32_t>(maxAnisotropy);
			graphDirty_ = true;
		}
		graphDirty_ |= MyGUI::DragFloat(
			"Mip LODバイアス", node.sampler.mipLODBias).valueChanged;
		graphDirty_ |= MyGUI::DragFloat(
			"最小LOD", node.sampler.minLOD).valueChanged;
		graphDirty_ |= MyGUI::DragFloat(
			"最大LOD", node.sampler.maxLOD).valueChanged;
		return;
	}

	if (node.kind == ShaderGraphNodeKind::SubGraph) {
		AssetID subGraph = node.subGraph;
		if (MyGUI::AssetReferenceField(
			"グラフ", subGraph,
			context.toolContext.assetDatabase,
			{ AssetType::ShaderGraph }).valueChanged) {
			node.subGraph = subGraph;
			node.inputPorts.clear();
			node.outputPorts.clear();
			std::erase_if(graph_.links,
				[&](const ShaderGraphLink& link) {
					return link.inputNode == node.id ||
						link.outputNode == node.id;
				});
			AssetDatabase* database =
				context.toolContext.assetDatabase;
			ShaderGraphAsset child{};
			const std::filesystem::path childPath =
				database && subGraph ?
					database->ResolveFullPath(subGraph) :
					std::filesystem::path{};
			if (!childPath.empty() &&
				FromJson(JsonAdapter::Load(childPath, true), child)) {
				for (const ShaderGraphParameter& parameter : child.parameters) {
					if (!parameter.exposed) {
						continue;
					}
					node.inputPorts.emplace_back(ShaderGraphPort{
						.id = UUID::New(),
						.name = parameter.name,
						.type = parameter.type,
						.defaultValue = parameter.defaultValue,
						});
				}
				const auto output = std::find_if(
					child.nodes.begin(), child.nodes.end(),
					[&](const ShaderGraphNode& value) {
						return value.id == child.outputNode;
					});
				if (output != child.nodes.end()) {
					const ShaderGraphNodeDescriptor* descriptor =
						ShaderGraphNodeRegistry::Find(output->kind);
					for (uint32_t slot = 0;
						slot < GetShaderGraphInputCount(*output); ++slot) {
						ShaderGraphValueType type = ShaderGraphValueType::Float;
						if (!output->inputPorts.empty()) {
							type = output->inputPorts[slot].type;
						} else if (descriptor && slot < descriptor->inputs.size()) {
							type = descriptor->inputs[slot].type;
						}
						node.outputPorts.emplace_back(ShaderGraphPort{
							.id = UUID::New(),
							.name = std::string(GetShaderGraphInputName(*output, slot)),
							.type = type,
							.defaultValue = DefaultValueForGraphType(type),
							});
					}
				}
			}
			graphDirty_ = true;
		}
		return;
	}
	if (node.kind != ShaderGraphNodeKind::CustomFunction) {
		return;
	}

	{
		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"ShaderGraphCustomFunction");
		graphDirty_ |= MyGUI::InputText(
			"関数名", node.functionName).valueChanged;
		graphDirty_ |= MyGUI::EnumCombo(
			"ソース", node.customFunctionSource).valueChanged;
		if (node.customFunctionSource ==
			ShaderGraphCustomFunctionSource::File) {
			AssetID functionFile = node.functionFileAsset;
			if (MyGUI::AssetReferenceField(
				"HLSLファイル", functionFile,
				context.toolContext.assetDatabase,
				{ AssetType::Shader }).valueChanged) {

				node.functionFileAsset = functionFile;
				node.functionFile.clear();
				graphDirty_ = true;
			}
		} else {
			TextEditSetting setting{};
			setting.multiLine = true;
			setting.size.y = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
			graphDirty_ |= MyGUI::InputText(
				"関数本体", node.functionBody, setting).valueChanged;
		}
	}

	const auto removePort = [&](bool input, uint32_t index) {
		std::erase_if(graph_.links,
			[&](const ShaderGraphLink& link) {
				return input ?
					(link.inputNode == node.id && link.inputSlot == index) :
					(link.outputNode == node.id && link.outputSlot == index);
			});
		for (ShaderGraphLink& link : graph_.links) {
			if (input && link.inputNode == node.id && index < link.inputSlot) {
				--link.inputSlot;
			} else if (!input && link.outputNode == node.id && index < link.outputSlot) {
				--link.outputSlot;
			}
		}
		auto& ports = input ? node.inputPorts : node.outputPorts;
		ports.erase(ports.begin() + index);
		graphDirty_ = true;
		};
	const auto drawPorts = [&](const char* label, bool input) {
		ImGui::SeparatorText(label);
		auto& ports = input ? node.inputPorts : node.outputPorts;
		for (uint32_t index = 0; index < ports.size();) {
			ShaderGraphPort& port = ports[index];
			ImGui::PushID(static_cast<int>(index) + (input ? 0 : 1000));
			MyGUI::ScopedPropertyLabelWidth labelWidth(
				input ? "ShaderGraphCustomInput" : "ShaderGraphCustomOutput");
			graphDirty_ |= MyGUI::InputText(
				"名前", port.name).valueChanged;
			const ShaderGraphValueType oldType = port.type;
			if (MyGUI::EnumCombo("型", port.type).valueChanged) {
				if (port.type == ShaderGraphValueType::Invalid ||
					port.type == ShaderGraphValueType::Texture2D ||
					port.type == ShaderGraphValueType::SamplerState) {
					port.type = oldType;
				} else {
					port.defaultValue = DefaultValueForGraphType(port.type);
					graphDirty_ = true;
				}
			}
			if (ImGui::Button("削除", ImVec2(-FLT_MIN, 0.0f))) {
				removePort(input, index);
				ImGui::PopID();
				continue;
			}
			ImGui::PopID();
			++index;
		}
		const char* addButtonLabel = input ?
			"追加##CustomFunctionInput" :
			"追加##CustomFunctionOutput";
		if (ImGui::Button(addButtonLabel, ImVec2(-FLT_MIN, 0.0f))) {
			const uint32_t suffix = static_cast<uint32_t>(ports.size() + 1);
			ports.emplace_back(ShaderGraphPort{
				.id = UUID::New(),
				.name = std::string(input ? "Input" : "Output") +
					std::to_string(suffix),
				.type = ShaderGraphValueType::Float,
				.defaultValue = DefaultValueForGraphType(
					ShaderGraphValueType::Float),
				});
			graphDirty_ = true;
		}
		};
	drawPorts("入力", true);
	drawPorts("出力", false);
}

void Engine::ShaderGraphEditorTool::DrawAppearancePanel() {

	if (!MyGUI::CollapsingHeader(
		"見た目設定", true)) {
		return;
	}

	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button(
		"保存##Appearance",
		ImVec2(buttonWidth, 0.0f))) {

		SaveAppearanceSettings();
		statusMessage_ = "見た目設定を保存しました";
	}
	ImGui::SameLine();
	if (ImGui::Button(
		"読み込み##Appearance",
		ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize =
			appearanceSetting_.nodePreviewTextureSize;
		const bool loaded = LoadAppearanceSettings();
		if (loaded &&
			previousTextureSize !=
				appearanceSetting_.nodePreviewTextureSize) {

			ClearNodePreviews();
		}
		statusMessage_ = loaded ?
			"見た目設定を読み込みました" :
			"保存済みの見た目設定がありません";
	}
	ImGui::SameLine();
	if (ImGui::Button(
		"元に戻す##Appearance",
		ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize =
			appearanceSetting_.nodePreviewTextureSize;
		RestoreDefaultAppearance();
		if (previousTextureSize !=
			appearanceSetting_.nodePreviewTextureSize) {

			ClearNodePreviews();
		}
		statusMessage_ = "見た目設定を既定値に戻しました";
	}

	if (MyGUI::CollapsingHeader(
		"キャンバス", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"ShaderGraphCanvasAppearance");
		MyGUI::ColorEdit(
			"背景", appearanceSetting_.canvasBackground);
		MyGUI::ColorEdit(
			"グリッド", appearanceSetting_.grid);
	}
	if (MyGUI::CollapsingHeader(
		"ノード", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"ShaderGraphNodeAppearance");
		MyGUI::ColorEdit(
			"背景", appearanceSetting_.nodeBackground);
		MyGUI::ColorEdit(
			"境界線", appearanceSetting_.nodeBorder);
		MyGUI::ColorEdit(
			"ホバー境界線",
			appearanceSetting_.hoveredNodeBorder);
		MyGUI::ColorEdit(
			"選択境界線",
			appearanceSetting_.selectedNodeBorder);
		MyGUI::ColorEdit(
			"範囲選択", appearanceSetting_.nodeSelection);
		MyGUI::ColorEdit(
			"範囲選択境界線",
			appearanceSetting_.nodeSelectionBorder);
		MyGUI::DragVector4(
			"余白", appearanceSetting_.nodePadding,
			AppearanceFloatSetting(0.0f, 64.0f));
		MyGUI::DragFloat(
			"文字スケール",
			appearanceSetting_.nodeTextScale,
			AppearanceFloatSetting(0.5f, 2.0f, 0.01f));
		MyGUI::DragFloat(
			"ノード最小幅",
			appearanceSetting_.nodeMinimumWidth,
			AppearanceFloatSetting(0.0f, 0.0f, 1.0f));
		MyGUI::DragFloat(
			"プレビュー表示サイズ",
			appearanceSetting_.nodePreviewDisplaySize,
			AppearanceFloatSetting(64.0f, 512.0f, 1.0f));
		const ValueEditResult previewTextureSizeResult =
			MyGUI::DragInt(
				"プレビュー解像度",
				appearanceSetting_.nodePreviewTextureSize,
				{
					.dragSpeed = 1.0f,
					.minValue = 32,
					.maxValue = 1024,
				});
		if (previewTextureSizeResult.editFinished) {
			ClampAppearanceSettings();
			ClearNodePreviews();
		}
		MyGUI::DragFloat(
			"角丸", appearanceSetting_.nodeRounding,
			AppearanceFloatSetting(0.0f, 32.0f));
		MyGUI::DragFloat(
			"境界線幅",
			appearanceSetting_.nodeBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat(
			"ホバー境界線幅",
			appearanceSetting_.hoveredNodeBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat(
			"選択境界線幅",
			appearanceSetting_.selectedNodeBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
	}
	if (MyGUI::CollapsingHeader(
		"接続", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"ShaderGraphConnectionAppearance");
		MyGUI::ColorEdit(
			"リンク", appearanceSetting_.link);
		MyGUI::ColorEdit(
			"ホバーリンク",
			appearanceSetting_.hoveredLinkBorder);
		MyGUI::ColorEdit(
			"選択リンク",
			appearanceSetting_.selectedLinkBorder);
		MyGUI::ColorEdit(
			"強調リンク",
			appearanceSetting_.highlightedLinkBorder);
		MyGUI::ColorEdit(
			"リンク範囲選択",
			appearanceSetting_.linkSelection);
		MyGUI::ColorEdit(
			"リンク範囲境界線",
			appearanceSetting_.linkSelectionBorder);
		MyGUI::ColorEdit(
			"ピン範囲選択",
			appearanceSetting_.pinSelection);
		MyGUI::ColorEdit(
			"ピン範囲境界線",
			appearanceSetting_.pinSelectionBorder);
		MyGUI::DragVector2(
			"開始位置オフセット",
			appearanceSetting_.linkStartOffset,
			AppearanceFloatSetting(-128.0f, 128.0f));
		MyGUI::DragVector2(
			"終了位置オフセット",
			appearanceSetting_.linkEndOffset,
			AppearanceFloatSetting(-128.0f, 128.0f));
		MyGUI::DragFloat(
			"ピン角丸",
			appearanceSetting_.pinRounding,
			AppearanceFloatSetting(0.0f, 16.0f));
		MyGUI::DragFloat(
			"ピン境界線幅",
			appearanceSetting_.pinBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat(
			"リンク曲率",
			appearanceSetting_.linkStrength,
			AppearanceFloatSetting(0.0f, 500.0f, 1.0f));
		MyGUI::DragFloat(
			"リンク太さ",
			appearanceSetting_.linkThickness,
			AppearanceFloatSetting(0.1f, 10.0f));
	}
	ClampAppearanceSettings();
}

void Engine::ShaderGraphEditorTool::DrawParameterEditor(
	const EditorToolContext& context,
	ShaderGraphParameter& parameter) {

	const std::string parameterID =
		ToString(parameter.id);
	ImGui::TextDisabled(
		"ID: %s", parameterID.c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("コピー##ParameterID")) {
		ImGui::SetClipboardText(
			parameterID.c_str());
		statusMessage_ =
			"パラメータIDをコピーしました";
	}

	graphDirty_ |= MyGUI::InputText(
		"名前", parameter.name).valueChanged;
	graphDirty_ |= MyGUI::InputText(
		"参照名", parameter.referenceName).valueChanged;
	graphDirty_ |= MyGUI::EnumCombo(
		"精度", parameter.precision).valueChanged;
	graphDirty_ |= MyGUI::EnumCombo(
		"更新単位", parameter.scope).valueChanged;
	graphDirty_ |= MyGUI::Checkbox(
		"公開", parameter.exposed);

	const ShaderGraphValueType oldType = parameter.type;
	if (MyGUI::EnumCombo(
		"型", parameter.type).valueChanged) {

		if (parameter.type == ShaderGraphValueType::Invalid ||
			parameter.type == ShaderGraphValueType::SamplerState) {
			parameter.type = oldType;
		} else {
			parameter.defaultValue =
				DefaultValueForGraphType(parameter.type);
			for (ShaderGraphNode& node : graph_.nodes) {
				if (node.kind == ShaderGraphNodeKind::Parameter &&
					node.parameterID == parameter.id) {

					node.valueType = parameter.type;
				}
			}
			graphDirty_ = true;
		}
	}
	graphDirty_ |= MyGUI::EnumCombo(
		"Semantic", parameter.semantic).valueChanged;

	switch (parameter.type) {
	case ShaderGraphValueType::Float: {
		float value =
			std::get_if<float>(&parameter.defaultValue.value) ?
			std::get<float>(parameter.defaultValue.value) : 0.0f;
		if (MyGUI::DragFloat(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float2: {
		Vector2 value =
			std::get_if<Vector2>(&parameter.defaultValue.value) ?
			std::get<Vector2>(parameter.defaultValue.value) : Vector2{};
		if (MyGUI::DragVector2(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float3: {
		Vector3 value =
			std::get_if<Vector3>(&parameter.defaultValue.value) ?
			std::get<Vector3>(parameter.defaultValue.value) : Vector3{};
		if (MyGUI::DragVector3(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float4: {
		Vector4 value =
			std::get_if<Vector4>(&parameter.defaultValue.value) ?
			std::get<Vector4>(parameter.defaultValue.value) : Vector4{};
		if (MyGUI::DragVector4(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Color: {
		Color4 value =
			std::get_if<Color4>(&parameter.defaultValue.value) ?
			std::get<Color4>(parameter.defaultValue.value) :
			Color4(1.0f, 1.0f, 1.0f, 1.0f);
		if (MyGUI::ColorEdit(
			"既定値", value,
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_NoInputs).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Texture2D: {
		AssetID value =
			std::get_if<AssetID>(&parameter.defaultValue.value) ?
			std::get<AssetID>(parameter.defaultValue.value) : AssetID{};
		AssetEditSetting setting{};
		setting.graphicsCore = context.panelContext ?
			context.panelContext->graphicsCore : nullptr;
		if (MyGUI::AssetReferenceField(
			"既定値", value,
			context.toolContext.assetDatabase,
			{ AssetType::Texture }, setting).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Boolean: {
		bool value =
			std::get_if<bool>(&parameter.defaultValue.value) ?
			std::get<bool>(parameter.defaultValue.value) : false;
		if (MyGUI::Checkbox("既定値", value)) {
			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Integer: {
		int32_t value =
			std::get_if<int32_t>(&parameter.defaultValue.value) ?
			std::get<int32_t>(parameter.defaultValue.value) : 0;
		if (MyGUI::DragInt("既定値", value).valueChanged) {
			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphEditorTool::DrawPreviewSetting(
	const EditorToolContext& context) {

	ImGui::SeparatorText("マテリアルプレビュー");

	ECSWorld* world = context.GetWorld();
	UUID nextEntity = previewEntityUUID_;
	MyGUI::ScopedPropertyLabelWidth labelWidth(
		"ShaderGraphMaterialPreview");
	MyGUI::BeginPropertyRow("プレビューエンティティ");
	const float clearButtonWidth = 72.0f;
	const float fieldWidth = (std::max)(
		ImGui::GetContentRegionAvail().x -
		clearButtonWidth -
		ImGui::GetStyle().ItemSpacing.x,
		1.0f);
	const ValueEditResult result =
		MyGUI::EntityReferenceField(
			"", nextEntity, world,
			{
				.useAutoPropertyRow = false,
				.buttonSize = ImVec2(
					fieldWidth,
					ImGui::GetFrameHeight()),
			});
	ImGui::SameLine();
	const bool hasPreviewEntity =
		previewEntityUUID_ != UUID{};
	ImGui::BeginDisabled(!hasPreviewEntity);
	const bool clear =
		ImGui::Button(
			"解除",
			ImVec2(
				clearButtonWidth,
				ImGui::GetFrameHeight()));
	ImGui::EndDisabled();
	MyGUI::EndPropertyRow();

	if (clear) {
		RestorePreviewMaterial(context);
		previewEntityUUID_ = {};
		return;
	}
	if (!result.valueChanged ||
		nextEntity == previewEntityUUID_) {

		return;
	}

	RestorePreviewMaterial(context);
	previewEntityUUID_ = nextEntity;
	if (!previewEntityUUID_) {
		return;
	}
	if ((!previewMaterial_ || previewCompileDirty_) &&
		!SaveAndCompile(context)) {

		return;
	}
	ApplyPreviewMaterial(context);
}

void Engine::ShaderGraphEditorTool::DrawGraph(
	const EditorToolContext& context) {

	if (!graphLoaded_) {
		return;
	}
	if (!nodeEditor_) {
		ed::Config config{};
		config.SettingsFile = nullptr;
		nodeEditor_ = ed::CreateEditor(&config);
		restoreNodePositions_ = true;
	}

	UpdateNodePreviews(context);
	ed::SetCurrentEditor(nodeEditor_);
	ApplyAppearanceSettings();
	ed::Begin("ShaderGraphNodeEditor");
	pinAddresses_.clear();
	for (ShaderGraphNode& node : graph_.nodes) {
		DrawNode(node);
	}
	for (ShaderGraphGroup& group : graph_.groups) {
		DrawGroup(group);
	}
	for (const ShaderGraphLink& link : graph_.links) {
		ed::Link(
			ed::LinkId(ToNodeEditorID(link.id)),
			ed::PinId(MakePinID(
				link.outputNode, false,
				link.outputSlot)),
			ed::PinId(MakePinID(
				link.inputNode, true,
				link.inputSlot)),
			ToImVec4(appearanceSetting_.link),
			appearanceSetting_.linkThickness);
	}

	if (restoreNodePositions_) {
		for (const ShaderGraphNode& node : graph_.nodes) {
			ed::SetNodePosition(
				ed::NodeId(ToNodeEditorID(node.id)),
				ImVec2(node.position.x, node.position.y));
		}
		for (const ShaderGraphGroup& group : graph_.groups) {
			const ed::NodeId groupID(
				ToNodeEditorID(group.id));
			ed::SetNodePosition(
				groupID,
				ImVec2(
					group.position.x,
					group.position.y));
			ed::SetGroupSize(
				groupID,
				ImVec2(group.size.x, group.size.y));
		}
		restoreNodePositions_ = false;
	}

	const ImVec4 linkColor =
		ToImVec4(appearanceSetting_.link);
	if (ed::BeginCreate(
		linkColor,
		appearanceSetting_.linkThickness)) {

		ed::PinId firstPin{};
		ed::PinId secondPin{};
		if (ed::QueryNewLink(
			&firstPin, &secondPin,
			linkColor,
			appearanceSetting_.linkThickness) &&
			firstPin && secondPin) {

			const auto first =
				pinAddresses_.find(firstPin.Get());
			const auto second =
				pinAddresses_.find(secondPin.Get());
			if (first != pinAddresses_.end() &&
				second != pinAddresses_.end() &&
				first->second.input != second->second.input) {

				const PinAddress& input =
					first->second.input ?
					first->second : second->second;
				const PinAddress& output =
					first->second.input ?
					second->second : first->second;
				if (ed::AcceptNewItem(
					linkColor,
					appearanceSetting_.linkThickness)) {

					std::erase_if(
						graph_.links,
						[&](const ShaderGraphLink& link) {
							return link.inputNode == input.node &&
								link.inputSlot == input.slot;
						});
					graph_.links.emplace_back(
						ShaderGraphLink{
							.id = UUID::New(),
							.outputNode = output.node,
							.outputSlot = output.slot,
							.inputNode = input.node,
							.inputSlot = input.slot,
						});
					graphDirty_ = true;
				}
			} else {
				ed::RejectNewItem(
					ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
					appearanceSetting_.linkThickness);
			}
		}
	}
	ed::EndCreate();

	if (commandPanelFocused_) {
		if (ed::BeginDelete()) {
			ed::LinkId linkID{};
			while (ed::QueryDeletedLink(&linkID)) {
				if (ed::AcceptDeletedItem()) {
					const uint64_t id =
						static_cast<uint64_t>(linkID.Get());
					std::erase_if(
						graph_.links,
						[&](const ShaderGraphLink& link) {
							return link.id.value == id;
						});
					graphDirty_ = true;
				}
			}

			ed::NodeId nodeID{};
			while (ed::QueryDeletedNode(&nodeID)) {
				const UUID id{
					static_cast<uint64_t>(nodeID.Get())
				};
				if (id == graph_.outputNode) {
					ed::RejectDeletedItem();
				} else if (ed::AcceptDeletedItem()) {
					const auto group = std::find_if(
						graph_.groups.begin(),
						graph_.groups.end(),
						[&](const ShaderGraphGroup& value) {
							return value.id == id;
						});
					if (group != graph_.groups.end()) {
						RemoveGroup(id);
					} else {
						RemoveNode(id);
					}
				}
			}
		}
		ed::EndDelete();
	}

	const ImVec2 canvasMousePosition =
		ImGui::GetMousePos();
	ed::Suspend();
	ed::NodeId contextNodeID{};
	if (ed::ShowNodeContextMenu(&contextNodeID)) {
		contextNode_ = UUID{
			static_cast<uint64_t>(
				contextNodeID.Get())
		};
		ImGui::OpenPopup(kNodeContextPopup);
	} else if (ed::ShowBackgroundContextMenu()) {
		createNodePosition_ =
			Vector2(
				canvasMousePosition.x,
				canvasMousePosition.y);
		ImGui::OpenPopup(kCreateNodePopup);
	}
	DrawContextMenus();
	ed::Resume();
	ed::End();
	const ImGuiIO& io = ImGui::GetIO();
	if (commandPanelFocused_ &&
		!io.WantTextInput &&
		!ImGui::IsAnyItemActive() &&
		io.KeyCtrl) {
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
			CopySelection();
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
			PasteSelection();
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
			CopySelection();
			PasteSelection();
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			SaveAndCompile(context);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			if (io.KeyShift) {
				RedoGraph();
			} else {
				UndoGraph();
			}
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
			RedoGraph();
		}
	}
	ed::SetCurrentEditor(nullptr);
}

void Engine::ShaderGraphEditorTool::UpdateNodePreviews(
	const EditorToolContext& context) {

	if (!previewState_ ||
		!context.panelContext ||
		!context.panelContext->graphicsCore) {

		return;
	}

	GraphicsCore& graphicsCore =
		*context.panelContext->graphicsCore;
	PreviewState& state = *previewState_;
	if (!state.pipelineAttempted) {

		state.pipelineAttempted = true;

		GraphicsPipelineDesc desc{};
		desc.type = PipelineType::Vertex;
		desc.preRaster.file =
			"Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl";
		desc.preRaster.entry = "main";
		desc.preRaster.profile = "vs_6_0";
		desc.pixel.file =
			"Builtin/ShaderGraphPreview/"
			"shaderGraphPreview.PS.hlsl";
		desc.pixel.entry = "main";
		desc.pixel.profile = "ps_6_6";

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter =
			D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU =
			D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV =
			D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW =
			D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.ComparisonFunc =
			D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility =
			D3D12_SHADER_VISIBILITY_PIXEL;
		desc.staticSamplers.emplace_back(sampler);

		desc.rasterizer =
			CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.rasterizer.CullMode =
			D3D12_CULL_MODE_NONE;
		desc.depthStencil =
			CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.depthStencil.DepthEnable = FALSE;
		desc.depthStencil.DepthWriteMask =
			D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.depthStencil.StencilEnable = FALSE;
		desc.sampleDesc = { 1, 0 };
		desc.topologyType =
			D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.numRenderTargets = 1;
		desc.rtvFormats[0] =
			DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.dsvFormat =
			DXGI_FORMAT_D24_UNORM_S8_UINT;

		state.pipelineInitialized =
			state.pipeline.CreateGraphics(
				graphicsCore.GetDXObject().GetDevice(),
				graphicsCore.GetDXObject().
					GetDxShaderCompiler(),
				desc);
	}
	if (!state.pipelineInitialized) {
		return;
	}

	// 展開中プレビューとその依存ノードに必要なRTだけを維持する
	const std::vector<const ShaderGraphNode*> previewOrder =
		BuildPreviewOrder(graph_);
	std::unordered_set<std::string> textureNames{};
	for (const ShaderGraphNode* node : previewOrder) {
		if (!node) {
			continue;
		}

		const std::string name =
			PreviewTextureName(node->id);
		textureNames.insert(name);
	}
	std::erase_if(
		state.retiredTextures,
		[&](const auto& retired) {
			return textureNames.contains(retired.first);
		});
	for (const std::string& name :
		state.textureNames) {

		if (!textureNames.contains(name)) {
			const auto found = std::find_if(
				state.retiredTextures.begin(),
				state.retiredTextures.end(),
				[&](const auto& retired) {
					return retired.first == name;
				});
			if (found == state.retiredTextures.end()) {
				state.retiredTextures.emplace_back(
					name,
					GraphicsFrameState::GetFrameSerial());
			}
		}
	}
	const uint64_t frameSerial =
		GraphicsFrameState::GetFrameSerial();
	std::erase_if(
		state.retiredTextures,
		[&](const auto& retired) {
			if (frameSerial < retired.second +
				kGraphicsFrameContextCount + 1u) {

				return false;
			}
			DestroyRenderTexture(retired.first);
			return true;
		});
	bool descriptorLimitReached = false;
	for (const std::string& name : textureNames) {
		if (!FindRenderTexture(name)) {
			const RTVDescriptor& rtvDescriptor =
				graphicsCore.GetRTVDescriptor();
			if (rtvDescriptor.GetMaxDescriptorCount() <=
				rtvDescriptor.GetUseDescriptorCount() +
					kNodePreviewRTVReserve ||
				!CreateRenderTexture(
					name,
					Vector2I(
						appearanceSetting_.
							nodePreviewTextureSize,
						appearanceSetting_.
							nodePreviewTextureSize),
					Color4::Black(), 1, false)) {

				descriptorLimitReached = true;
			}
		}
	}
	if (descriptorLimitReached &&
		!state.descriptorLimitReached) {

		statusMessage_ =
			"ノードプレビュー用RTVの空きがありません";
	}
	state.descriptorLimitReached =
		descriptorLimitReached;
	state.textureNames = std::move(textureNames);

	std::unordered_map<uint64_t, uint32_t>
		textureIndices{};
	for (const ShaderGraphNode* node : previewOrder) {
		if (!node) {
			continue;
		}

		uint32_t textureIndex = UINT32_MAX;
		const PreviewTextureReference reference =
			ResolvePreviewTextureReference(
				graph_, *node);
		if (reference.assetID) {
			const GPUTextureResource* texture =
				RuntimeTextureResolver::Resolve(
					graphicsCore,
					context.toolContext.assetDatabase,
					reference.assetID,
					reference.sRGB);
			if (texture && texture->valid) {
				textureIndex = texture->srvIndex;
			}
		}
		textureIndices[node->id.value] =
			textureIndex;
	}

	const uint64_t graphHash =
		CalculatePreviewHash(graph_);
	const bool textureChanged =
		textureIndices != state.textureIndices;
	const bool timeDependent =
		std::any_of(
			previewOrder.begin(), previewOrder.end(),
			[](const ShaderGraphNode* node) {
				return node &&
					node->kind == ShaderGraphNodeKind::Time;
			});
	if (state.previewsValid &&
		state.graphHash == graphHash &&
		!textureChanged && !timeDependent) {

		return;
	}

	state.textureIndices =
		std::move(textureIndices);
	state.graphHash = graphHash;
	state.previewsValid = false;

	const GPUTextureResource* whiteTexture =
		graphicsCore.GetBuiltinTextureLibrary().
			GetWhiteTexture();
	const D3D12_GPU_DESCRIPTOR_HANDLE fallbackHandle =
		whiteTexture && whiteTexture->valid ?
			whiteTexture->gpuHandle :
			D3D12_GPU_DESCRIPTOR_HANDLE{};

	for (const ShaderGraphNode* node : previewOrder) {

		if (!node) {
			continue;
		}
		EditorToolRenderTexture* destination =
			FindRenderTexture(
				PreviewTextureName(node->id));
		if (!destination ||
			!destination->IsValid()) {

			continue;
		}

		PreviewConstants constants{};
		const float previewTime =
			static_cast<float>(ImGui::GetTime());
		const float previewDeltaTime =
			ImGui::GetIO().DeltaTime;
		constants.timeValues = Vector4(
			previewTime,
			std::sin(previewTime),
			std::cos(previewTime),
			previewDeltaTime);
		constants.operation =
			static_cast<uint32_t>(
				GetPreviewOperation(graph_, *node));
		constants.outputValueType =
			static_cast<uint32_t>(
				ResolvePreviewOutputType(
					graph_, *node));
		const auto textureIndex =
			state.textureIndices.find(
				node->id.value);
		if (textureIndex !=
			state.textureIndices.end()) {

			constants.textureIndex =
				textureIndex->second;
		}

		if (node->kind ==
			ShaderGraphNodeKind::Parameter) {

			const ShaderGraphParameter* parameter =
				FindPreviewParameter(
					graph_, node->parameterID);
			if (parameter) {
				constants.literalValue =
					ToPreviewVector(
						parameter->defaultValue,
						parameter->type);
				if (parameter->type ==
					ShaderGraphValueType::Texture2D) {

					constants.literalValue =
						Vector4(
							1.0f, 1.0f,
							1.0f, 1.0f);
				}
			}
		} else {
			constants.literalValue =
				ToPreviewVector(
					node->value,
					node->kind ==
						ShaderGraphNodeKind::TextureSample ?
						ShaderGraphValueType::Color :
						node->valueType);
		}

		std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4>
			inputHandles{
				fallbackHandle, fallbackHandle,
				fallbackHandle, fallbackHandle,
			};
		for (uint32_t inputSlot = 0;
			inputSlot < inputHandles.size();
			++inputSlot) {

			constants.inputDefaults[inputSlot] =
				PreviewInputDefault(
					node->kind, inputSlot);
			const ShaderGraphLink* link =
				FindPreviewInput(
					graph_, *node, inputSlot);
			if (!link) {
				continue;
			}

			const ShaderGraphNode* source =
				FindPreviewNode(
					graph_, link->outputNode);
			if (source && source->kind ==
				ShaderGraphNodeKind::Time) {

				const std::array timeOutputs{
					previewTime,
					std::sin(previewTime),
					std::cos(previewTime),
					previewDeltaTime,
					previewDeltaTime,
				};
				if (link->outputSlot < timeOutputs.size()) {
					const float value =
						timeOutputs[link->outputSlot];
					constants.inputDefaults[inputSlot] =
						Vector4(value, value, value, value);
				}
				continue;
			}
			const EditorToolRenderTexture*
				sourceTexture = source ?
				FindRenderTexture(
					PreviewTextureName(
						source->id)) :
				nullptr;
			const RenderTexture2D* sourceValue =
				sourceTexture ?
				sourceTexture->GetColorTexture(0) :
				nullptr;
			if (!source || !sourceValue) {
				continue;
			}

			inputHandles[inputSlot] =
				sourceValue->GetSRVGPUHandle();
			constants.connectedMask |=
				1u << inputSlot;
			constants.inputSwizzles[inputSlot] =
				static_cast<uint32_t>(
					ResolvePreviewSwizzle(
						graph_, *source,
						link->outputSlot));
		}

		RenderToTexture(
			*destination,
			[&](const EditorToolRenderContext&
				renderContext) {

				ID3D12GraphicsCommandList*
					commandList =
					renderContext.dxCommand->
						GetCommandList();
				commandList->SetGraphicsRootSignature(
					state.pipeline.GetRootSignature());
				commandList->SetPipelineState(
					state.pipeline.GetGraphicsPipeline(
						BlendMode::Normal));

				state.bindCache.Sync(state.pipeline);
				for (uint32_t inputSlot = 0;
					inputSlot <
						inputHandles.size();
					++inputSlot) {

					if (state.bindCache.Has(
						state.inputSlots[inputSlot])) {

						RootBindingCommand::
							SetGraphicsSRV(
								commandList,
								state.bindCache.Get(
									state.inputSlots[
										inputSlot]),
								0,
								inputHandles[
									inputSlot]);
					}
				}

				DxConstBuffer<PreviewConstants>&
					constantBuffer =
					state.AllocateConstantBuffer(
						graphicsCore);
				constantBuffer.TransferData(constants);
				if (state.bindCache.Has(
					state.constantsSlot)) {

					RootBindingCommand::
						SetGraphicsCBV(
							commandList,
							state.bindCache.Get(
								state.constantsSlot),
							constantBuffer.GetResource()->
								GetGPUVirtualAddress());
				}

				commandList->IASetPrimitiveTopology(
					D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				commandList->DrawInstanced(
					3, 1, 0, 0);
			});
	}
	state.previewsValid = true;
}

void Engine::ShaderGraphEditorTool::InvalidateNodePreviews() {

	if (!previewState_) {
		return;
	}
	previewState_->graphHash = 0;
	previewState_->previewsValid = false;
}

void Engine::ShaderGraphEditorTool::ClearNodePreviews() {

	if (!previewState_) {
		return;
	}

	std::unordered_set<std::string> textureNames =
		previewState_->textureNames;
	for (const auto& retired :
		previewState_->retiredTextures) {

		textureNames.insert(retired.first);
	}
	for (const std::string& name : textureNames) {

		DestroyRenderTexture(name);
	}
	previewState_->textureNames.clear();
	previewState_->retiredTextures.clear();
	previewState_->textureIndices.clear();
	previewState_->graphHash = 0;
	previewState_->previewsValid = false;
	previewState_->descriptorLimitReached = false;
}

void Engine::ShaderGraphEditorTool::DrawGroup(
	ShaderGraphGroup& group) {

	ImGui::PushFont(
		nullptr,
		ImGui::GetStyle().FontSizeBase *
		appearanceSetting_.nodeTextScale);
	ed::PushStyleVar(
		ed::StyleVar_NodePadding,
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	const ed::NodeId groupID(
		ToNodeEditorID(group.id));
	const ImVec2 editorGroupSize =
		ed::GetNodeSize(groupID);
	const float groupWidth =
		0.0f < editorGroupSize.x ?
			editorGroupSize.x : group.size.x;
	ed::BeginNode(
		groupID);
	ImGui::PushID(
		static_cast<int>(group.id.value));

	const ImVec2 headerMinimum =
		ImGui::GetCursorScreenPos();
	if (editingGroup_ == group.id) {
		const float editWidth =
			(std::min)(groupWidth, kGroupNameEditWidth);
		ImGui::SetCursorScreenPos(
			ImVec2(
				headerMinimum.x +
					(groupWidth - editWidth) * 0.5f,
				headerMinimum.y));
		ImGui::SetNextItemWidth(editWidth);
		if (requestGroupNameFocus_) {
			ImGui::SetKeyboardFocusHere();
			requestGroupNameFocus_ = false;
		}

		const std::string previousName = group.name;
		const bool committed = ImGui::InputText(
			"##GroupName",
			&group.name,
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_AutoSelectAll);
		const bool deactivated =
			ImGui::IsItemDeactivated();
		if (group.name != previousName) {
			graphDirty_ = true;
		}
		if (committed || deactivated) {
			if (group.name.empty()) {
				group.name = "Group";
				graphDirty_ = true;
			}
			editingGroup_ = UUID{};
		}
	} else {
		const char* name =
			group.name.empty() ? "Group" :
			group.name.c_str();
		const float textWidth =
			ImGui::CalcTextSize(name).x;
		ImGui::SetCursorScreenPos(
			ImVec2(
				headerMinimum.x +
					(groupWidth - textWidth) * 0.5f,
				headerMinimum.y));
		ImGui::TextUnformatted(name);
		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(
				ImGuiMouseButton_Left)) {

			editingGroup_ = group.id;
			requestGroupNameFocus_ = true;
		}
	}

	const float groupMinimumY =
		ImGui::GetCursorScreenPos().y;
	ImGui::SetCursorScreenPos(
		ImVec2(headerMinimum.x, groupMinimumY));
	ed::Group(
		ImVec2(groupWidth, group.size.y));
	ImGui::PopID();
	ed::EndNode();
	std::vector<ed::NodeId> memberIDs{};
	for (const ShaderGraphNode& node : graph_.nodes) {
		if (node.groupID == group.id) {
			memberIDs.emplace_back(
				ToNodeEditorID(node.id));
		}
	}
	ed::SetGroupMembers(
		groupID,
		memberIDs.empty() ? nullptr : memberIDs.data(),
		static_cast<int>(memberIDs.size()));
	ed::PopStyleVar();

	if (ed::BeginGroupHint(
		groupID)) {

		const ImVec2 groupMinimum =
			ed::GetGroupMin();
		const ImVec2 groupMaximum =
			ed::GetGroupMax();
		const Vector2 groupSize{
			groupMaximum.x - groupMinimum.x,
			groupMaximum.y - groupMinimum.y,
		};
		if (group.size.x != groupSize.x ||
			group.size.y != groupSize.y) {

			group.size = groupSize;
			graphDirty_ = true;
		}
	}
	ed::EndGroupHint();
	ImGui::PopFont();
}

void Engine::ShaderGraphEditorTool::DrawNode(
	ShaderGraphNode& node) {

	ImGui::PushFont(
		nullptr,
		ImGui::GetStyle().FontSizeBase *
		appearanceSetting_.nodeTextScale);
	ed::BeginNode(
		ed::NodeId(ToNodeEditorID(node.id)));
	ImGui::PushID(
		static_cast<int>(node.id.value));
	const float nodeWidth =
		CalculateNodeWidth(node);
	const ImVec2 headerMinimum =
		ImGui::GetCursorScreenPos();
	ImGui::TextUnformatted(
		GetShaderGraphNodeName(node.kind).data());
	float headerHeight =
		ImGui::GetItemRectSize().y;
	if (IsPreviewableNode(node.kind)) {

		const float buttonSize =
			ImGui::GetFrameHeight();
		ImGui::SetCursorScreenPos(
			ImVec2(
				headerMinimum.x +
					nodeWidth - buttonSize,
				headerMinimum.y));
		if (ImGui::ArrowButton(
			"##NodePreview",
			node.previewExpanded ?
				ImGuiDir_Down :
				ImGuiDir_Right)) {

			node.previewExpanded =
				!node.previewExpanded;
			graphDirty_ = true;
		}
		headerHeight =
			(std::max)(
				headerHeight,
				ImGui::GetItemRectSize().y);
	}
	ImGui::SetCursorScreenPos(
		ImVec2(
			headerMinimum.x,
			headerMinimum.y + headerHeight));
	DrawNodeSeparator(nodeWidth);

	if (node.kind == ShaderGraphNodeKind::Parameter) {
		const auto parameter = std::find_if(
			graph_.parameters.begin(),
			graph_.parameters.end(),
			[&](const ShaderGraphParameter& value) {
				return value.id == node.parameterID;
			});
		if (parameter != graph_.parameters.end()) {
			ImGui::TextDisabled(
				"%s", parameter->name.c_str());
		}
	}
	if (node.kind == ShaderGraphNodeKind::Keyword) {
		const auto keyword = std::find_if(
			graph_.keywords.begin(), graph_.keywords.end(),
			[&](const ShaderGraphKeyword& value) {
				return value.id == node.keywordID;
			});
		if (keyword != graph_.keywords.end()) {
			ImGui::TextDisabled("%s", keyword->name.c_str());
		}
	}
	if (node.kind == ShaderGraphNodeKind::CustomFunction &&
		!node.functionName.empty()) {
		ImGui::TextDisabled("%s", node.functionName.c_str());
	}
	if (node.kind == ShaderGraphNodeKind::Constant ||
		node.kind == ShaderGraphNodeKind::TextureSample) {

		DrawNodeValue(node, nodeWidth);
	}
	DrawNodePins(node, nodeWidth);
	DrawNodePreview(node, nodeWidth);
	ImGui::PopID();
	ed::EndNode();
	ImGui::PopFont();
}

void Engine::ShaderGraphEditorTool::DrawNodePreview(
	ShaderGraphNode& node,
	float nodeWidth) {

	if (!IsPreviewableNode(node.kind) ||
		!node.previewExpanded) {

		return;
	}

	const EditorToolRenderTexture* texture =
		FindRenderTexture(
			PreviewTextureName(node.id));
	const ImTextureID textureID =
		texture ?
			texture->GetImTextureID(0) :
			static_cast<ImTextureID>(0);
	const float displaySize =
		(std::min)(
			nodeWidth,
			appearanceSetting_.
				nodePreviewDisplaySize);
	const float offsetX =
		(nodeWidth - displaySize) * 0.5f;
	const ImVec2 rowMinimum =
		ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(
		ImVec2(
			rowMinimum.x + offsetX,
			rowMinimum.y));

	const ImVec2 imageMinimum =
		ImGui::GetCursorScreenPos();
	const ImVec2 imageMaximum{
		imageMinimum.x + displaySize,
		imageMinimum.y + displaySize,
	};
	ImDrawList* drawList =
		ImGui::GetWindowDrawList();
	constexpr float checkerSize = 8.0f;
	const ImU32 checkerColors[2]{
		IM_COL32(58, 58, 58, 255),
		IM_COL32(92, 92, 92, 255),
	};
	for (float y = imageMinimum.y;
		y < imageMaximum.y;
		y += checkerSize) {

		for (float x = imageMinimum.x;
			x < imageMaximum.x;
			x += checkerSize) {

			const int32_t column =
				static_cast<int32_t>(
					(x - imageMinimum.x) /
					checkerSize);
			const int32_t row =
				static_cast<int32_t>(
					(y - imageMinimum.y) /
					checkerSize);
			drawList->AddRectFilled(
				ImVec2(x, y),
				ImVec2(
					(std::min)(
						x + checkerSize,
						imageMaximum.x),
					(std::min)(
						y + checkerSize,
						imageMaximum.y)),
				checkerColors[
					(column + row) & 1]);
		}
	}

	if (textureID) {
		ImGui::Image(
			textureID,
			ImVec2(displaySize, displaySize));
	} else {
		ImGui::Dummy(
			ImVec2(displaySize, displaySize));
	}
	drawList->AddRect(
		imageMinimum, imageMaximum,
		ImGui::GetColorU32(
			ImGuiCol_Border));
	ImGui::SetCursorScreenPos(
		ImVec2(
			rowMinimum.x,
			imageMaximum.y));
	ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
}

void Engine::ShaderGraphEditorTool::DrawNodePins(
	const ShaderGraphNode& node,
	float nodeWidth) {

	const uint32_t inputCount =
		GetShaderGraphInputCount(node);
	const uint32_t outputCount =
		GetShaderGraphOutputCount(node);
	const uint32_t rowCount =
		(std::max)(inputCount, outputCount);
	if (rowCount == 0) {
		ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
		return;
	}

	const ImVec2 rowsMinimum =
		ImGui::GetCursorScreenPos();
	const float rowHeight =
		ImGui::GetTextLineHeightWithSpacing();
	for (uint32_t row = 0;
		row < rowCount; ++row) {

		const float rowY =
			rowsMinimum.y + rowHeight * row;
		if (row < inputCount) {
			const std::string name =
				"> " + std::string(
					GetShaderGraphInputName(
						node, row));
			ImGui::SetCursorScreenPos(
				ImVec2(rowsMinimum.x, rowY));

			const uintptr_t pinID =
				MakePinID(node.id, true, row);
			pinAddresses_[pinID] =
				PinAddress{ node.id, row, true };
			ed::BeginPin(
				ed::PinId(pinID),
				ed::PinKind::Input);
			ImGui::TextUnformatted(name.c_str());
			const ImVec2 pinMinimum =
				ImGui::GetItemRectMin();
			const ImVec2 pinMaximum =
				ImGui::GetItemRectMax();
			const ImVec2 pinCenter{
				pinMinimum.x +
					appearanceSetting_.linkEndOffset.x,
				(pinMinimum.y + pinMaximum.y) * 0.5f +
					appearanceSetting_.linkEndOffset.y,
			};
			ed::PinPivotRect(pinCenter, pinCenter);
			ed::EndPin();
		}
		if (row < outputCount) {
			const std::string name =
				std::string(
					GetShaderGraphOutputName(
						node, row)) +
				" >";
			const float textWidth =
				ImGui::CalcTextSize(name.c_str()).x;
			ImGui::SetCursorScreenPos(
				ImVec2(
					rowsMinimum.x +
						nodeWidth - textWidth,
					rowY));

			const uintptr_t pinID =
				MakePinID(node.id, false, row);
			pinAddresses_[pinID] =
				PinAddress{ node.id, row, false };
			ed::BeginPin(
				ed::PinId(pinID),
				ed::PinKind::Output);
			ImGui::TextUnformatted(name.c_str());
			const ImVec2 pinMinimum =
				ImGui::GetItemRectMin();
			const ImVec2 pinMaximum =
				ImGui::GetItemRectMax();
			const ImVec2 pinCenter{
				pinMaximum.x +
					appearanceSetting_.linkStartOffset.x,
				(pinMinimum.y + pinMaximum.y) * 0.5f +
					appearanceSetting_.linkStartOffset.y,
			};
			ed::PinPivotRect(pinCenter, pinCenter);
			ed::EndPin();
		}
	}

	ImGui::SetCursorScreenPos(
		ImVec2(
			rowsMinimum.x,
			rowsMinimum.y + rowHeight * rowCount));
	ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
}

void Engine::ShaderGraphEditorTool::DrawNodeSeparator(
	float nodeWidth) const {

	const ImVec2 minimum =
		ImGui::GetCursorScreenPos();
	const float height =
		ImGui::GetStyle().ItemSpacing.y;
	const float lineY =
		minimum.y + height * 0.5f;
	ImGui::GetWindowDrawList()->AddLine(
		ImVec2(minimum.x, lineY),
		ImVec2(minimum.x + nodeWidth, lineY),
		ImGui::GetColorU32(ImGuiCol_Separator));
	ImGui::Dummy(ImVec2(nodeWidth, height));
}

float Engine::ShaderGraphEditorTool::CalculateNodeWidth(
	const ShaderGraphNode& node) const {

	float nodeWidth =
		(std::max)(
			appearanceSetting_.nodeMinimumWidth,
			ImGui::CalcTextSize(
				GetShaderGraphNodeName(
					node.kind).data()).x +
				(IsPreviewableNode(node.kind) ?
					ImGui::GetStyle().ItemSpacing.x +
					ImGui::GetFrameHeight() : 0.0f));
	if (node.kind == ShaderGraphNodeKind::Parameter) {
		const auto parameter = std::find_if(
			graph_.parameters.begin(),
			graph_.parameters.end(),
			[&](const ShaderGraphParameter& value) {
				return value.id == node.parameterID;
			});
		if (parameter != graph_.parameters.end()) {
			nodeWidth =
				(std::max)(
					nodeWidth,
					ImGui::CalcTextSize(
						parameter->name.c_str()).x);
		}
	}
	if (IsPreviewableNode(node.kind) &&
		node.previewExpanded) {
		nodeWidth =
			(std::max)(
				nodeWidth,
				appearanceSetting_.
					nodePreviewDisplaySize);
	}

	float inputWidth = 0.0f;
	for (uint32_t slot = 0;
		slot < GetShaderGraphInputCount(node);
		++slot) {

		const std::string name =
			"> " + std::string(
				GetShaderGraphInputName(
					node, slot));
		inputWidth =
			(std::max)(
				inputWidth,
				ImGui::CalcTextSize(name.c_str()).x);
	}
	float outputWidth = 0.0f;
	for (uint32_t slot = 0;
		slot < GetShaderGraphOutputCount(node);
		++slot) {

		const std::string name =
			std::string(
				GetShaderGraphOutputName(
					node, slot)) +
			" >";
		outputWidth =
			(std::max)(
				outputWidth,
				ImGui::CalcTextSize(name.c_str()).x);
	}
	if (inputWidth != 0.0f &&
		outputWidth != 0.0f) {

		nodeWidth =
			(std::max)(
				nodeWidth,
				inputWidth +
					kNodePinColumnGap +
					outputWidth);
	} else {
		nodeWidth =
			(std::max)(
				nodeWidth,
				(std::max)(
					inputWidth,
					outputWidth));
	}
	return nodeWidth;
}

void Engine::ShaderGraphEditorTool::DrawNodeValue(
	ShaderGraphNode& node,
	float nodeWidth) {

	if (node.kind == ShaderGraphNodeKind::Constant) {
		const ShaderGraphValueType oldType =
			node.valueType;
		ComboEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("型", nodeWidth);
		if (MyGUI::EnumCombo(
			"型", node.valueType,
			setting).valueChanged) {

			if (node.valueType ==
				ShaderGraphValueType::Invalid ||
				node.valueType ==
				ShaderGraphValueType::Texture2D ||
				node.valueType ==
				ShaderGraphValueType::SamplerState) {

				node.valueType = oldType;
			} else {
				node.value =
					DefaultValueForGraphType(
						node.valueType);
				graphDirty_ = true;
			}
		}
	}

	const ShaderGraphValueType valueType =
		node.kind ==
			ShaderGraphNodeKind::TextureSample ?
		ShaderGraphValueType::Color :
		node.valueType;
	switch (valueType) {
	case ShaderGraphValueType::Float: {
		float value =
			std::get_if<float>(&node.value.value) ?
			std::get<float>(node.value.value) : 0.0f;
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragFloat(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float2: {
		Vector2 value =
			std::get_if<Vector2>(&node.value.value) ?
			std::get<Vector2>(node.value.value) :
			Vector2{};
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector2(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float3: {
		Vector3 value =
			std::get_if<Vector3>(&node.value.value) ?
			std::get<Vector3>(node.value.value) :
			Vector3{};
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector3(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float4: {
		Vector4 value =
			std::get_if<Vector4>(&node.value.value) ?
			std::get<Vector4>(node.value.value) :
			Vector4{};
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector4(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Color: {
		Color4 value =
			std::get_if<Color4>(&node.value.value) ?
			std::get<Color4>(node.value.value) :
			Color4::White();
		const char* label =
			node.kind ==
				ShaderGraphNodeKind::TextureSample ?
			"未設定時" : "値";
		if (DrawNodeColorEdit(
			label, value,
			nodeWidth).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Boolean: {
		bool value =
			std::get_if<bool>(&node.value.value) ?
			std::get<bool>(node.value.value) : false;
		if (MyGUI::Checkbox(
			"値", value,
			NodeValueRowSetting("値", nodeWidth))) {
			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Integer: {
		int32_t value =
			std::get_if<int32_t>(&node.value.value) ?
			std::get<int32_t>(node.value.value) : 0;
		IntEditSetting setting{};
		setting.propertyRow = NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragInt("値", value, setting).valueChanged) {
			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphEditorTool::DrawContextMenus() {

	if (ImGui::BeginPopup(kNodeContextPopup)) {
		const auto group = std::find_if(
			graph_.groups.begin(),
			graph_.groups.end(),
			[&](const ShaderGraphGroup& value) {
				return value.id == contextNode_;
			});
		const auto node = std::find_if(
			graph_.nodes.begin(),
			graph_.nodes.end(),
			[&](const ShaderGraphNode& value) {
				return value.id == contextNode_;
			});

		if (group != graph_.groups.end()) {
			ImGui::TextUnformatted(group->name.c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("グループを複製")) {
				DuplicateGroup(contextNode_);
			}
			if (ImGui::MenuItem("グループを削除")) {
				RemoveGroup(contextNode_);
			}
		} else {
			if (node != graph_.nodes.end()) {
				ImGui::TextUnformatted(
					GetShaderGraphNodeName(
						node->kind).data());
			}
			ImGui::Separator();
			if (node != graph_.nodes.end() &&
				IsPreviewableNode(node->kind)) {

				if (ImGui::MenuItem(
					"プレビューを表示",
					nullptr,
					node->previewExpanded)) {

					node->previewExpanded =
						!node->previewExpanded;
					graphDirty_ = true;
				}
				ImGui::Separator();
			}
			ImGui::BeginDisabled(
				node == graph_.nodes.end() ||
				contextNode_ == graph_.outputNode);
			if (ImGui::MenuItem("ノードを複製")) {
				DuplicateNode(contextNode_);
			}
			if (ImGui::MenuItem("ノードを削除")) {
				RemoveNode(contextNode_);
			}
			ImGui::EndDisabled();

			const bool canGroup =
				2 <= GetSelectedGraphNodes().size();
			ImGui::BeginDisabled(!canGroup);
			if (ImGui::MenuItem(
				"選択ノードをグループ化")) {

				GroupSelectedNodes();
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}

	DrawNodeCreationMenu();
}

void Engine::ShaderGraphEditorTool::DrawNodeCreationMenu() {

	if (!ImGui::BeginPopup(kCreateNodePopup)) {
		return;
	}

	const bool canGroup =
		2 <= GetSelectedGraphNodes().size();
	ImGui::BeginDisabled(!canGroup);
	if (ImGui::MenuItem(
		"選択ノードをグループ化")) {

		GroupSelectedNodes();
	}
	ImGui::EndDisabled();
	if (ImGui::BeginMenu("プレビュー")) {
		if (ImGui::MenuItem("すべて展開")) {
			for (ShaderGraphNode& node :
				graph_.nodes) {

				if (IsPreviewableNode(node.kind)) {
					node.previewExpanded = true;
				}
			}
			graphDirty_ = true;
		}
		if (ImGui::MenuItem("すべて折りたたむ")) {
			for (ShaderGraphNode& node :
				graph_.nodes) {

				if (IsPreviewableNode(node.kind)) {
					node.previewExpanded = false;
				}
			}
			graphDirty_ = true;
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	ImGui::SetNextItemWidth(280.0f);
	ImGui::InputTextWithHint(
		"##NodeSearch", "ノードを検索",
		&nodeSearch_);
	if (graph_.domain == ShaderGraphDomain::Surface &&
		SupportsShaderGraphVertexOutput(graph_.target) &&
		!graph_.vertexOutputNode &&
		ImGui::MenuItem("頂点出力を追加")) {

		AddNode(ShaderGraphNodeKind::VertexOutput,
			createNodePosition_);
	}
	if (ImGui::BeginMenu("定数")) {
		const std::array constantTypes{
			ShaderGraphValueType::Float,
			ShaderGraphValueType::Float2,
			ShaderGraphValueType::Float3,
			ShaderGraphValueType::Float4,
			ShaderGraphValueType::Color,
			ShaderGraphValueType::Boolean,
			ShaderGraphValueType::Integer,
		};
		for (ShaderGraphValueType type : constantTypes) {
			if (ImGui::MenuItem(
				EnumAdapter<ShaderGraphValueType>::ToString(type))) {
				AddConstantNode(type, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}

	const std::string search = Algorithm::ToLower(nodeSearch_);
	const auto isCreatable = [](const ShaderGraphNodeDescriptor& descriptor) {
		return descriptor.kind != ShaderGraphNodeKind::SurfaceOutput &&
			descriptor.kind != ShaderGraphNodeKind::UnlitOutput &&
			descriptor.kind != ShaderGraphNodeKind::PostProcessOutput &&
			descriptor.kind != ShaderGraphNodeKind::VertexOutput &&
			descriptor.kind != ShaderGraphNodeKind::Parameter &&
			descriptor.kind != ShaderGraphNodeKind::Constant &&
			descriptor.kind != ShaderGraphNodeKind::Keyword;
		};
	const auto& descriptors =
		ShaderGraphNodeRegistry::GetDescriptors();
	if (!search.empty()) {
		for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
			if (!isCreatable(descriptor)) {
				continue;
			}
			const std::string searchable = Algorithm::ToLower(
				std::string(descriptor.name) + " " +
				std::string(descriptor.category));
			if (searchable.find(search) != std::string::npos &&
				ImGui::MenuItem(descriptor.name.data())) {
				AddNode(descriptor.kind, createNodePosition_);
			}
		}
	} else {
		std::vector<std::string_view> categories;
		for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
			if (isCreatable(descriptor) &&
				std::find(categories.begin(), categories.end(), descriptor.category) == categories.end()) {
				categories.emplace_back(descriptor.category);
			}
		}
		for (std::string_view category : categories) {
			if (!ImGui::BeginMenu(category.data())) {
				continue;
			}
			for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
				if (isCreatable(descriptor) && descriptor.category == category &&
					ImGui::MenuItem(descriptor.name.data())) {
					AddNode(descriptor.kind, createNodePosition_);
				}
			}
			ImGui::EndMenu();
		}
	}
	if (ImGui::BeginMenu("パラメータ")) {
		for (const ShaderGraphParameter& parameter :
			graph_.parameters) {

			if (ImGui::MenuItem(
				parameter.name.c_str())) {

				AddParameterNode(
					parameter.id,
					createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("キーワード")) {
		for (const ShaderGraphKeyword& keyword : graph_.keywords) {
			if (ImGui::MenuItem(keyword.name.c_str())) {
				AddKeywordNode(keyword.id, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

bool Engine::ShaderGraphEditorTool::LoadGraph(
	const EditorToolContext& context,
	AssetID assetID) {

	RestorePreviewMaterial(context);
	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || !assetID) {
		selectedAsset_ = {};
		previewMaterial_ = {};
		graphLoaded_ = false;
		graphDirty_ = false;
		previewCompileDirty_ = false;
		compiledGraphState_.clear();
		ClearNodePreviews();
		ResetNodeEditor();
		return false;
	}

	const std::filesystem::path path =
		database->ResolveFullPath(assetID);
	ShaderGraphAsset loaded{};
	if (path.empty() ||
		!FromJson(JsonAdapter::Load(path, false), loaded)) {

		statusMessage_ =
			"グラフを読み込めませんでした";
		return false;
	}

	ClearNodePreviews();
	selectedAsset_ = assetID;
	graph_ = std::move(loaded);
	history_.Reset(graph_);
	const std::filesystem::path materialPath =
		path.parent_path() /
		Algorithm::PathFromUTF8(
			GraphFileStem(path) + ".material.json");
	previewMaterial_ = std::filesystem::exists(materialPath) ?
		database->ImportOrGet(
			RuntimePaths::ToAssetPath(materialPath),
			AssetType::Material) : AssetID{};
	compiledGraphState_ = MakeShaderGraphCompileState(graph_);
	previewCompileDirty_ = !previewMaterial_;
	if (previewMaterial_) {
		MaterialAsset material{};
		const MaterialAsset expected =
			ShaderGraphArtifactCache::CreateMaterial(
				graph_, selectedAsset_);
		previewCompileDirty_ =
			!FromJson(
				JsonAdapter::Load(materialPath, false), material) ||
			!HasCurrentGraphDefaults(material, expected);
	}
	graphLoaded_ = true;
	graphDirty_ = false;
	selectedParameter_ = -1;
	latestDiagnostics_.clear();
	statusMessage_.clear();
	ResetNodeEditor();
	restoreNodePositions_ = true;
	return true;
}

bool Engine::ShaderGraphEditorTool::CreateGraph(
	const EditorToolContext& context) {

	RestorePreviewMaterial(context);
	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || createAssetPath_.empty()) {
		statusMessage_ = "作成先を設定してください";
		return false;
	}

	const std::filesystem::path path =
		database->ResolveAssetPath(createAssetPath_);
	if (path.empty()) {
		statusMessage_ = "GameAssets内を指定してください";
		return false;
	}
	if (std::filesystem::exists(path)) {
		statusMessage_ = "同名のグラフが存在します";
		return false;
	}

	const std::string name = GraphFileStem(path);
	ShaderGraphAsset graph =
		createDomain_ == ShaderGraphDomain::PostProcess ?
			CreateDefaultPostProcessShaderGraph(name) :
			CreateDefaultSurfaceShaderGraph(name, createTarget_);
	JsonAdapter::Save(path, ToJson(graph));

	const std::string assetPath =
		RuntimePaths::ToAssetPath(path);
	const AssetID assetID =
		database->ImportOrGet(
			assetPath, AssetType::ShaderGraph);
	if (!assetID) {
		statusMessage_ =
			"グラフをAssetDatabaseへ登録できませんでした";
		return false;
	}
	statusMessage_ = "グラフを作成しました";
	return LoadGraph(context, assetID);
}

bool Engine::ShaderGraphEditorTool::SaveAndCompile(
	const EditorToolContext& context) {

	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || !selectedAsset_) {
		return false;
	}
	const std::filesystem::path graphPath =
		database->ResolveFullPath(selectedAsset_);
	if (graphPath.empty()) {
		statusMessage_ = "グラフのパスを解決できません";
		return false;
	}

	CaptureNodePositions();
	ShaderGraphArtifact artifact{};
	if (!ShaderGraphArtifactCache::Compile(
		graph_, selectedAsset_, artifact, database)) {
		latestDiagnostics_ = artifact.compileOutput.diagnostics;
		statusMessage_ =
			artifact.compileOutput.diagnostics.empty() ?
			"派生Shaderを生成できませんでした" :
			artifact.compileOutput.diagnostics.front().message;
		return false;
	}
	latestDiagnostics_ = artifact.compileOutput.diagnostics;

	const std::string stem = GraphFileStem(graphPath);
	const std::filesystem::path materialPath =
		graphPath.parent_path() /
		Algorithm::PathFromUTF8(stem + ".material.json");
	MaterialAsset material =
		ShaderGraphArtifactCache::CreateMaterial(
			graph_, selectedAsset_);
	ShaderGraphArtifactCache::ApplyToMaterial(
		artifact, material);
	JsonAdapter::Save(materialPath, ToJson(material));
	previewMaterial_ = database->ImportOrGet(
		RuntimePaths::ToAssetPath(materialPath),
		AssetType::Material);
	if (!previewMaterial_) {
		statusMessage_ =
			"Materialを登録できませんでした";
		return false;
	}

	JsonAdapter::Save(graphPath, ToJson(graph_));
	graphDirty_ = false;
	if (context.panelContext &&
		context.panelContext->renderPipeline) {

		RenderPipelineRunner& renderPipeline =
			*context.panelContext->renderPipeline;
		renderPipeline.ReloadMaterial(previewMaterial_);
		if (artifact.opaqueShaderID) {
			renderPipeline.ReloadShader(artifact.opaqueShaderID);
		}
		if (artifact.transparentShaderID) {
			renderPipeline.ReloadShader(artifact.transparentShaderID);
		}
		if (artifact.depthShaderID) {
			renderPipeline.ReloadShader(artifact.depthShaderID);
		}
		if (artifact.pickingShaderID) {
			renderPipeline.ReloadShader(artifact.pickingShaderID);
		}
		if (artifact.computeShaderID) {
			renderPipeline.ReloadShader(artifact.computeShaderID);
		}
		RenderAssetLibrary& library =
			renderPipeline.GetRenderAssetLibrary();
		library.RegisterDerivedShader(
			std::move(artifact.opaqueShader));
		library.RegisterDerivedShader(
			std::move(artifact.transparentShader));
		library.RegisterDerivedShader(
			std::move(artifact.depthShader));
		library.RegisterDerivedShader(
			std::move(artifact.pickingShader));
		library.RegisterDerivedShader(
			std::move(artifact.computeShader));
		library.RegisterDerivedPipeline(
			std::move(artifact.opaquePipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.transparentPipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.depthPipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.pickingPipeline));
		material.guid = previewMaterial_;
		ShaderGraphArtifactCache::ApplyToMaterial(
			artifact, material);
		library.RegisterDerivedMaterial(std::move(material));
	}
	compiledGraphState_ = MakeShaderGraphCompileState(graph_);
	previewCompileDirty_ = false;
	statusMessage_ = "コンパイルしました";
	return true;
}

bool Engine::ShaderGraphEditorTool::ApplyPreviewMaterial(
	const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !previewEntityUUID_ ||
		!previewMaterial_) {

		return false;
	}
	const Entity entity =
		world->FindByUUID(previewEntityUUID_);
	if (!world->IsAlive(entity)) {
		previewEntityUUID_ = {};
		previewMaterialApplied_ = false;
		return false;
	}

	if (previewMaterialApplied_ &&
		(appliedPreviewEntityUUID_ !=
			previewEntityUUID_ ||
			appliedPreviewTarget_ != graph_.target)) {

		RestorePreviewMaterial(context);
	}
	if (!previewMaterialApplied_) {
		if (!ReadRendererMaterial(
			*world, entity, graph_.target,
			previewOriginalMaterial_)) {

			statusMessage_ =
				"描画対象に対応するRendererがありません";
			return false;
		}
		appliedPreviewEntityUUID_ =
			previewEntityUUID_;
		appliedPreviewTarget_ = graph_.target;
		previewMaterialApplied_ = true;
	}
	if (!WriteRendererMaterial(
		*world, entity, graph_.target,
		previewMaterial_)) {

		previewMaterialApplied_ = false;
		return false;
	}
	statusMessage_ =
		"マテリアルをプレビューしています";
	return true;
}

void Engine::ShaderGraphEditorTool::RestorePreviewMaterial(
	const EditorToolContext& context) {

	if (!previewMaterialApplied_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	if (world) {
		const Entity entity =
			world->FindByUUID(
				appliedPreviewEntityUUID_);
		if (world->IsAlive(entity)) {
			WriteRendererMaterial(
				*world, entity,
				appliedPreviewTarget_,
				previewOriginalMaterial_);
		}
	}
	appliedPreviewEntityUUID_ = {};
	previewOriginalMaterial_ = {};
	previewMaterialApplied_ = false;
	previewCompileDeadline_ = 0.0;
}

void Engine::ShaderGraphEditorTool::UpdateMaterialPreview(
	const EditorToolContext& context) {

	if (graph_.domain != ShaderGraphDomain::Surface) {
		previewCompileDeadline_ = 0.0;
		return;
	}
	if (!previewEntityUUID_) {
		previewCompileDeadline_ = 0.0;
		return;
	}
	if (!previewCompileDirty_) {
		previewCompileDeadline_ = 0.0;
		if (!previewMaterialApplied_) {
			ApplyPreviewMaterial(context);
		}
		return;
	}
	if (ImGui::IsAnyItemActive()) {
		previewCompileDeadline_ = 0.0;
		return;
	}

	const double now = ImGui::GetTime();
	if (previewCompileDeadline_ <= 0.0) {
		previewCompileDeadline_ = now + 0.25;
		return;
	}
	if (now < previewCompileDeadline_) {
		return;
	}

	previewCompileDeadline_ = 0.0;
	if (SaveAndCompile(context)) {
		ApplyPreviewMaterial(context);
	}
}

bool Engine::ShaderGraphEditorTool::LoadAppearanceSettings() {

	const std::filesystem::path path =
		RuntimePaths::GetUserSettingsPath(
			ConfigPaths::kShaderGraphAppearance);
	if (!JsonAdapter::Check(path)) {
		return false;
	}

	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return false;
	}

	RestoreDefaultAppearance();
	const auto colorsFound = data.find("colors");
	if (colorsFound != data.end() &&
		colorsFound->is_object()) {

		const nlohmann::json& colors = *colorsFound;
		appearanceSetting_.canvasBackground =
			ReadColor(colors, "canvasBackground",
				appearanceSetting_.canvasBackground);
		appearanceSetting_.grid =
			ReadColor(colors, "grid",
				appearanceSetting_.grid);
		appearanceSetting_.nodeBackground =
			ReadColor(colors, "nodeBackground",
				appearanceSetting_.nodeBackground);
		appearanceSetting_.nodeBorder =
			ReadColor(colors, "nodeBorder",
				appearanceSetting_.nodeBorder);
		appearanceSetting_.hoveredNodeBorder =
			ReadColor(colors, "hoveredNodeBorder",
				appearanceSetting_.hoveredNodeBorder);
		appearanceSetting_.selectedNodeBorder =
			ReadColor(colors, "selectedNodeBorder",
				appearanceSetting_.selectedNodeBorder);
		appearanceSetting_.nodeSelection =
			ReadColor(colors, "nodeSelection",
				appearanceSetting_.nodeSelection);
		appearanceSetting_.nodeSelectionBorder =
			ReadColor(colors, "nodeSelectionBorder",
				appearanceSetting_.nodeSelectionBorder);
		appearanceSetting_.link =
			ReadColor(colors, "link",
				appearanceSetting_.link);
		appearanceSetting_.hoveredLinkBorder =
			ReadColor(colors, "hoveredLinkBorder",
				appearanceSetting_.hoveredLinkBorder);
		appearanceSetting_.selectedLinkBorder =
			ReadColor(colors, "selectedLinkBorder",
				appearanceSetting_.selectedLinkBorder);
		appearanceSetting_.highlightedLinkBorder =
			ReadColor(colors, "highlightedLinkBorder",
				appearanceSetting_.highlightedLinkBorder);
		appearanceSetting_.linkSelection =
			ReadColor(colors, "linkSelection",
				appearanceSetting_.linkSelection);
		appearanceSetting_.linkSelectionBorder =
			ReadColor(colors, "linkSelectionBorder",
				appearanceSetting_.linkSelectionBorder);
		appearanceSetting_.pinSelection =
			ReadColor(colors, "pinSelection",
				appearanceSetting_.pinSelection);
		appearanceSetting_.pinSelectionBorder =
			ReadColor(colors, "pinSelectionBorder",
				appearanceSetting_.pinSelectionBorder);
	}

	const auto layoutFound = data.find("layout");
	if (layoutFound != data.end() &&
		layoutFound->is_object()) {

		const nlohmann::json& layout = *layoutFound;
		appearanceSetting_.nodePadding =
			ReadVector4(layout, "nodePadding",
				appearanceSetting_.nodePadding);
		appearanceSetting_.linkStartOffset =
			ReadVector2(layout, "linkStartOffset",
				appearanceSetting_.linkStartOffset);
		appearanceSetting_.linkEndOffset =
			ReadVector2(layout, "linkEndOffset",
				appearanceSetting_.linkEndOffset);
		appearanceSetting_.nodeMinimumWidth =
			layout.value("nodeMinimumWidth",
				appearanceSetting_.nodeMinimumWidth);
		appearanceSetting_.nodePreviewDisplaySize =
			layout.value("nodePreviewDisplaySize",
				appearanceSetting_.nodePreviewDisplaySize);
		appearanceSetting_.nodePreviewTextureSize =
			layout.value("nodePreviewTextureSize",
				appearanceSetting_.nodePreviewTextureSize);
		appearanceSetting_.nodeRounding =
			layout.value("nodeRounding",
				appearanceSetting_.nodeRounding);
		appearanceSetting_.nodeBorderWidth =
			layout.value("nodeBorderWidth",
				appearanceSetting_.nodeBorderWidth);
		appearanceSetting_.hoveredNodeBorderWidth =
			layout.value("hoveredNodeBorderWidth",
				appearanceSetting_.hoveredNodeBorderWidth);
		appearanceSetting_.selectedNodeBorderWidth =
			layout.value("selectedNodeBorderWidth",
				appearanceSetting_.selectedNodeBorderWidth);
		appearanceSetting_.nodeTextScale =
			layout.value("nodeTextScale",
				appearanceSetting_.nodeTextScale);
		appearanceSetting_.pinRounding =
			layout.value("pinRounding",
				appearanceSetting_.pinRounding);
		appearanceSetting_.pinBorderWidth =
			layout.value("pinBorderWidth",
				appearanceSetting_.pinBorderWidth);
		appearanceSetting_.linkStrength =
			layout.value("linkStrength",
				appearanceSetting_.linkStrength);
		appearanceSetting_.linkThickness =
			layout.value("linkThickness",
				appearanceSetting_.linkThickness);
	}
	ClampAppearanceSettings();
	return true;
}

void Engine::ShaderGraphEditorTool::SaveAppearanceSettings() const {

	nlohmann::json colors{
		{ "canvasBackground",
			appearanceSetting_.canvasBackground.ToJson() },
		{ "grid", appearanceSetting_.grid.ToJson() },
		{ "nodeBackground",
			appearanceSetting_.nodeBackground.ToJson() },
		{ "nodeBorder",
			appearanceSetting_.nodeBorder.ToJson() },
		{ "hoveredNodeBorder",
			appearanceSetting_.hoveredNodeBorder.ToJson() },
		{ "selectedNodeBorder",
			appearanceSetting_.selectedNodeBorder.ToJson() },
		{ "nodeSelection",
			appearanceSetting_.nodeSelection.ToJson() },
		{ "nodeSelectionBorder",
			appearanceSetting_.nodeSelectionBorder.ToJson() },
		{ "link", appearanceSetting_.link.ToJson() },
		{ "hoveredLinkBorder",
			appearanceSetting_.hoveredLinkBorder.ToJson() },
		{ "selectedLinkBorder",
			appearanceSetting_.selectedLinkBorder.ToJson() },
		{ "highlightedLinkBorder",
			appearanceSetting_.highlightedLinkBorder.ToJson() },
		{ "linkSelection",
			appearanceSetting_.linkSelection.ToJson() },
		{ "linkSelectionBorder",
			appearanceSetting_.linkSelectionBorder.ToJson() },
		{ "pinSelection",
			appearanceSetting_.pinSelection.ToJson() },
		{ "pinSelectionBorder",
			appearanceSetting_.pinSelectionBorder.ToJson() },
	};
	nlohmann::json layout{
		{ "nodePadding",
			appearanceSetting_.nodePadding.ToJson() },
		{ "linkStartOffset",
			appearanceSetting_.linkStartOffset.ToJson() },
		{ "linkEndOffset",
			appearanceSetting_.linkEndOffset.ToJson() },
		{ "nodeMinimumWidth",
			appearanceSetting_.nodeMinimumWidth },
		{ "nodePreviewDisplaySize",
			appearanceSetting_.nodePreviewDisplaySize },
		{ "nodePreviewTextureSize",
			appearanceSetting_.nodePreviewTextureSize },
		{ "nodeRounding",
			appearanceSetting_.nodeRounding },
		{ "nodeBorderWidth",
			appearanceSetting_.nodeBorderWidth },
		{ "hoveredNodeBorderWidth",
			appearanceSetting_.hoveredNodeBorderWidth },
		{ "selectedNodeBorderWidth",
			appearanceSetting_.selectedNodeBorderWidth },
		{ "nodeTextScale",
			appearanceSetting_.nodeTextScale },
		{ "pinRounding",
			appearanceSetting_.pinRounding },
		{ "pinBorderWidth",
			appearanceSetting_.pinBorderWidth },
		{ "linkStrength",
			appearanceSetting_.linkStrength },
		{ "linkThickness",
			appearanceSetting_.linkThickness },
	};
	const nlohmann::json data{
		{ "schemaVersion", 3 },
		{ "colors", std::move(colors) },
		{ "layout", std::move(layout) },
	};
	JsonAdapter::Save(
		RuntimePaths::GetUserSettingsPath(
			ConfigPaths::kShaderGraphAppearance),
		data);
}

void Engine::ShaderGraphEditorTool::ApplyAppearanceSettings() {

	ed::Style& style = ed::GetStyle();
	style.Colors[ed::StyleColor_Bg] =
		ToImVec4(appearanceSetting_.canvasBackground);
	style.Colors[ed::StyleColor_Grid] =
		ToImVec4(appearanceSetting_.grid);
	style.Colors[ed::StyleColor_NodeBg] =
		ToImVec4(appearanceSetting_.nodeBackground);
	style.Colors[ed::StyleColor_NodeBorder] =
		ToImVec4(appearanceSetting_.nodeBorder);
	style.Colors[ed::StyleColor_HovNodeBorder] =
		ToImVec4(appearanceSetting_.hoveredNodeBorder);
	style.Colors[ed::StyleColor_SelNodeBorder] =
		ToImVec4(appearanceSetting_.selectedNodeBorder);
	style.Colors[ed::StyleColor_NodeSelRect] =
		ToImVec4(appearanceSetting_.nodeSelection);
	style.Colors[ed::StyleColor_NodeSelRectBorder] =
		ToImVec4(appearanceSetting_.nodeSelectionBorder);
	style.Colors[ed::StyleColor_HovLinkBorder] =
		ToImVec4(appearanceSetting_.hoveredLinkBorder);
	style.Colors[ed::StyleColor_SelLinkBorder] =
		ToImVec4(appearanceSetting_.selectedLinkBorder);
	style.Colors[ed::StyleColor_HighlightLinkBorder] =
		ToImVec4(appearanceSetting_.highlightedLinkBorder);
	style.Colors[ed::StyleColor_LinkSelRect] =
		ToImVec4(appearanceSetting_.linkSelection);
	style.Colors[ed::StyleColor_LinkSelRectBorder] =
		ToImVec4(appearanceSetting_.linkSelectionBorder);
	style.Colors[ed::StyleColor_PinRect] =
		ToImVec4(appearanceSetting_.pinSelection);
	style.Colors[ed::StyleColor_PinRectBorder] =
		ToImVec4(appearanceSetting_.pinSelectionBorder);

	style.NodePadding = ImVec4(
		appearanceSetting_.nodePadding.x,
		appearanceSetting_.nodePadding.y,
		appearanceSetting_.nodePadding.z,
		appearanceSetting_.nodePadding.w);
	style.NodeRounding =
		appearanceSetting_.nodeRounding;
	style.NodeBorderWidth =
		appearanceSetting_.nodeBorderWidth;
	style.HoveredNodeBorderWidth =
		appearanceSetting_.hoveredNodeBorderWidth;
	style.SelectedNodeBorderWidth =
		appearanceSetting_.selectedNodeBorderWidth;
	style.PinRounding =
		appearanceSetting_.pinRounding;
	style.PinBorderWidth =
		appearanceSetting_.pinBorderWidth;
	style.LinkStrength =
		appearanceSetting_.linkStrength;
}

void Engine::ShaderGraphEditorTool::RestoreDefaultAppearance() {

	const ed::Style style{};
	appearanceSetting_.canvasBackground =
		ToColor4(style.Colors[ed::StyleColor_Bg]);
	appearanceSetting_.grid =
		ToColor4(style.Colors[ed::StyleColor_Grid]);
	appearanceSetting_.nodeBackground =
		ToColor4(style.Colors[ed::StyleColor_NodeBg]);
	appearanceSetting_.nodeBorder =
		ToColor4(style.Colors[ed::StyleColor_NodeBorder]);
	appearanceSetting_.hoveredNodeBorder =
		ToColor4(style.Colors[ed::StyleColor_HovNodeBorder]);
	appearanceSetting_.selectedNodeBorder =
		ToColor4(style.Colors[ed::StyleColor_SelNodeBorder]);
	appearanceSetting_.nodeSelection =
		ToColor4(style.Colors[ed::StyleColor_NodeSelRect]);
	appearanceSetting_.nodeSelectionBorder =
		ToColor4(style.Colors[
			ed::StyleColor_NodeSelRectBorder]);
	appearanceSetting_.link = Color4::White();
	appearanceSetting_.hoveredLinkBorder =
		ToColor4(style.Colors[ed::StyleColor_HovLinkBorder]);
	appearanceSetting_.selectedLinkBorder =
		ToColor4(style.Colors[ed::StyleColor_SelLinkBorder]);
	appearanceSetting_.highlightedLinkBorder =
		ToColor4(style.Colors[
			ed::StyleColor_HighlightLinkBorder]);
	appearanceSetting_.linkSelection =
		ToColor4(style.Colors[ed::StyleColor_LinkSelRect]);
	appearanceSetting_.linkSelectionBorder =
		ToColor4(style.Colors[
			ed::StyleColor_LinkSelRectBorder]);
	appearanceSetting_.pinSelection =
		ToColor4(style.Colors[ed::StyleColor_PinRect]);
	appearanceSetting_.pinSelectionBorder =
		ToColor4(style.Colors[ed::StyleColor_PinRectBorder]);

	appearanceSetting_.nodePadding = Vector4(
		style.NodePadding.x, style.NodePadding.y,
		style.NodePadding.z, style.NodePadding.w);
	appearanceSetting_.linkStartOffset = Vector2{};
	appearanceSetting_.linkEndOffset = Vector2{};
	appearanceSetting_.nodeMinimumWidth =
		kDefaultNodeMinimumWidth;
	appearanceSetting_.nodePreviewDisplaySize =
		kDefaultNodePreviewDisplaySize;
	appearanceSetting_.nodePreviewTextureSize =
		kDefaultNodePreviewTextureSize;
	appearanceSetting_.nodeRounding =
		style.NodeRounding;
	appearanceSetting_.nodeBorderWidth =
		style.NodeBorderWidth;
	appearanceSetting_.hoveredNodeBorderWidth =
		style.HoveredNodeBorderWidth;
	appearanceSetting_.selectedNodeBorderWidth =
		style.SelectedNodeBorderWidth;
	appearanceSetting_.nodeTextScale = 1.0f;
	appearanceSetting_.pinRounding =
		style.PinRounding;
	appearanceSetting_.pinBorderWidth =
		style.PinBorderWidth;
	appearanceSetting_.linkStrength =
		style.LinkStrength;
	appearanceSetting_.linkThickness = 1.0f;
}

void Engine::ShaderGraphEditorTool::ClampAppearanceSettings() {

	appearanceSetting_.nodePadding.x =
		(std::clamp)(appearanceSetting_.nodePadding.x, 0.0f, 64.0f);
	appearanceSetting_.nodePadding.y =
		(std::clamp)(appearanceSetting_.nodePadding.y, 0.0f, 64.0f);
	appearanceSetting_.nodePadding.z =
		(std::clamp)(appearanceSetting_.nodePadding.z, 0.0f, 64.0f);
	appearanceSetting_.nodePadding.w =
		(std::clamp)(appearanceSetting_.nodePadding.w, 0.0f, 64.0f);
	appearanceSetting_.linkStartOffset.x =
		(std::clamp)(appearanceSetting_.linkStartOffset.x, -128.0f, 128.0f);
	appearanceSetting_.linkStartOffset.y =
		(std::clamp)(appearanceSetting_.linkStartOffset.y, -128.0f, 128.0f);
	appearanceSetting_.linkEndOffset.x =
		(std::clamp)(appearanceSetting_.linkEndOffset.x, -128.0f, 128.0f);
	appearanceSetting_.linkEndOffset.y =
		(std::clamp)(appearanceSetting_.linkEndOffset.y, -128.0f, 128.0f);
	appearanceSetting_.nodePreviewDisplaySize =
		(std::clamp)(
			appearanceSetting_.nodePreviewDisplaySize,
			64.0f, 512.0f);
	appearanceSetting_.nodePreviewTextureSize =
		(std::clamp)(
			appearanceSetting_.nodePreviewTextureSize,
			32, 1024);
	appearanceSetting_.nodeRounding =
		(std::clamp)(appearanceSetting_.nodeRounding, 0.0f, 32.0f);
	appearanceSetting_.nodeBorderWidth =
		(std::clamp)(appearanceSetting_.nodeBorderWidth, 0.0f, 10.0f);
	appearanceSetting_.hoveredNodeBorderWidth =
		(std::clamp)(appearanceSetting_.hoveredNodeBorderWidth, 0.0f, 10.0f);
	appearanceSetting_.selectedNodeBorderWidth =
		(std::clamp)(appearanceSetting_.selectedNodeBorderWidth, 0.0f, 10.0f);
	appearanceSetting_.nodeTextScale =
		(std::clamp)(appearanceSetting_.nodeTextScale, 0.5f, 2.0f);
	appearanceSetting_.pinRounding =
		(std::clamp)(appearanceSetting_.pinRounding, 0.0f, 16.0f);
	appearanceSetting_.pinBorderWidth =
		(std::clamp)(appearanceSetting_.pinBorderWidth, 0.0f, 10.0f);
	appearanceSetting_.linkStrength =
		(std::clamp)(appearanceSetting_.linkStrength, 0.0f, 500.0f);
	appearanceSetting_.linkThickness =
		(std::clamp)(appearanceSetting_.linkThickness, 0.1f, 10.0f);
}

void Engine::ShaderGraphEditorTool::CaptureNodePositions() {

	if (!nodeEditor_) {
		return;
	}
	ed::SetCurrentEditor(nodeEditor_);
	for (ShaderGraphNode& node : graph_.nodes) {
		const ImVec2 position =
			ed::GetNodePosition(
				ed::NodeId(ToNodeEditorID(node.id)));
		node.position =
			Vector2(position.x, position.y);
	}
	for (ShaderGraphGroup& group : graph_.groups) {
		const ed::NodeId groupID(
			ToNodeEditorID(group.id));
		const ImVec2 position =
			ed::GetNodePosition(groupID);
		group.position =
			Vector2(position.x, position.y);
	}
	ed::SetCurrentEditor(nullptr);
}

void Engine::ShaderGraphEditorTool::ResetNodeEditor() {

	if (nodeEditor_) {
		ed::DestroyEditor(nodeEditor_);
		nodeEditor_ = nullptr;
	}
	pinAddresses_.clear();
}

void Engine::ShaderGraphEditorTool::RemoveNode(
	UUID nodeID) {

	if (nodeID == graph_.vertexOutputNode) {
		graph_.vertexOutputNode = UUID{};
	}

	std::erase_if(
		graph_.nodes,
		[&](const ShaderGraphNode& node) {
			return node.id == nodeID;
		});
	std::erase_if(
		graph_.links,
		[&](const ShaderGraphLink& link) {
			return link.inputNode == nodeID ||
				link.outputNode == nodeID;
		});
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::RemoveGroup(
	UUID groupID) {

	if (editingGroup_ == groupID) {
		editingGroup_ = UUID{};
		requestGroupNameFocus_ = false;
	}
	for (ShaderGraphNode& node : graph_.nodes) {
		if (node.groupID == groupID) {
			node.groupID = UUID{};
		}
	}
	std::erase_if(
		graph_.groups,
		[&](const ShaderGraphGroup& group) {
			return group.id == groupID;
		});
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::DuplicateNode(
	UUID nodeID) {

	const auto found = std::find_if(
		graph_.nodes.begin(),
		graph_.nodes.end(),
		[&](const ShaderGraphNode& node) {
			return node.id == nodeID;
		});
	if (found == graph_.nodes.end() ||
		nodeID == graph_.outputNode ||
		nodeID == graph_.vertexOutputNode) {
		return;
	}

	const ImVec2 sourcePosition =
		ed::GetNodePosition(
			ed::NodeId(ToNodeEditorID(nodeID)));
	ShaderGraphNode duplicate = *found;
	duplicate.id = UUID::New();
	duplicate.position = Vector2(
		sourcePosition.x + kDuplicateOffset,
		sourcePosition.y + kDuplicateOffset);
	for (ShaderGraphPort& port : duplicate.inputPorts) {
		port.id = UUID::New();
	}
	for (ShaderGraphPort& port : duplicate.outputPorts) {
		port.id = UUID::New();
	}
	const UUID duplicateID = duplicate.id;
	graph_.nodes.emplace_back(std::move(duplicate));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(duplicateID)),
		ImVec2(
			sourcePosition.x + kDuplicateOffset,
			sourcePosition.y + kDuplicateOffset));

	ed::ClearSelection();
	ed::SelectNode(
		ed::NodeId(ToNodeEditorID(duplicateID)));
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::DuplicateGroup(
	UUID groupID) {

	const auto found = std::find_if(
		graph_.groups.begin(),
		graph_.groups.end(),
		[&](const ShaderGraphGroup& group) {
			return group.id == groupID;
		});
	if (found == graph_.groups.end()) {
		return;
	}

	const ShaderGraphGroup sourceGroup = *found;
	const ed::NodeId sourceGroupID(
		ToNodeEditorID(groupID));
	const ImVec2 sourcePosition =
		ed::GetNodePosition(sourceGroupID);
	const ImVec2 sourceSize =
		ed::GetNodeSize(sourceGroupID);
	const float sourceWidth =
		0.0f < sourceSize.x ?
			sourceSize.x : sourceGroup.size.x;
	const ImVec2 duplicateOffset(
		sourceWidth + kDuplicateGroupSpacing, 0.0f);
	ShaderGraphGroup duplicateGroup = sourceGroup;
	duplicateGroup.id = UUID::New();
	duplicateGroup.name += " コピー";
	duplicateGroup.position = Vector2(
		sourcePosition.x + duplicateOffset.x,
		sourcePosition.y + duplicateOffset.y);
	duplicateGroup.size = Vector2(
		0.0f < sourceSize.x ?
			sourceSize.x : sourceGroup.size.x,
		0.0f < sourceSize.y ?
			sourceSize.y : sourceGroup.size.y);
	const UUID newGroupID = duplicateGroup.id;

	// グループ内ノードと内部リンクだけを複製
	std::unordered_map<uint64_t, UUID> duplicateNodeIDs{};
	const size_t sourceNodeCount = graph_.nodes.size();
	for (size_t index = 0;
		index < sourceNodeCount; ++index) {

		const ShaderGraphNode& source =
			graph_.nodes[index];
		if (source.groupID != groupID ||
			source.id == graph_.outputNode ||
			source.id == graph_.vertexOutputNode) {

			continue;
		}

		const ed::NodeId sourceNodeID(
			ToNodeEditorID(source.id));
		const ImVec2 nodePosition =
			ed::GetNodePosition(sourceNodeID);
		ShaderGraphNode duplicate = source;
		duplicate.id = UUID::New();
		duplicate.groupID = newGroupID;
		duplicate.position = Vector2(
			nodePosition.x + duplicateOffset.x,
			nodePosition.y + duplicateOffset.y);
		for (ShaderGraphPort& port : duplicate.inputPorts) {
			port.id = UUID::New();
		}
		for (ShaderGraphPort& port : duplicate.outputPorts) {
			port.id = UUID::New();
		}
		duplicateNodeIDs.emplace(
			source.id.value, duplicate.id);
		const UUID duplicateID = duplicate.id;
		graph_.nodes.emplace_back(std::move(duplicate));
		ed::SetNodePosition(
			ed::NodeId(ToNodeEditorID(duplicateID)),
			ImVec2(
				nodePosition.x + duplicateOffset.x,
				nodePosition.y + duplicateOffset.y));
	}

	const size_t sourceLinkCount = graph_.links.size();
	for (size_t index = 0;
		index < sourceLinkCount; ++index) {

		const ShaderGraphLink& source =
			graph_.links[index];
		const auto output = duplicateNodeIDs.find(
			source.outputNode.value);
		const auto input = duplicateNodeIDs.find(
			source.inputNode.value);
		if (output == duplicateNodeIDs.end() ||
			input == duplicateNodeIDs.end()) {
			continue;
		}

		ShaderGraphLink duplicate = source;
		duplicate.id = UUID::New();
		duplicate.outputNode = output->second;
		duplicate.inputNode = input->second;
		graph_.links.emplace_back(std::move(duplicate));
	}

	graph_.groups.emplace_back(std::move(duplicateGroup));

	const ed::NodeId newEditorGroupID(
		ToNodeEditorID(newGroupID));
	ed::SetNodePosition(
		newEditorGroupID,
		ImVec2(
			sourcePosition.x + duplicateOffset.x,
			sourcePosition.y + duplicateOffset.y));
	ed::SetGroupSize(
		newEditorGroupID,
		ImVec2(
			graph_.groups.back().size.x,
			graph_.groups.back().size.y));
	ed::ClearSelection();
	ed::SelectNode(newEditorGroupID);
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::RemoveParameter(
	uint32_t index) {

	if (graph_.parameters.size() <= index) {
		return;
	}
	const UUID parameterID =
		graph_.parameters[index].id;
	std::vector<UUID> nodes;
	for (const ShaderGraphNode& node : graph_.nodes) {
		if (node.kind == ShaderGraphNodeKind::Parameter &&
			node.parameterID == parameterID) {

			nodes.emplace_back(node.id);
		}
	}
	for (UUID nodeID : nodes) {
		RemoveNode(nodeID);
	}
	graph_.parameters.erase(
		graph_.parameters.begin() + index);
	selectedParameter_ = -1;
	graphDirty_ = true;
}

std::vector<Engine::UUID>
Engine::ShaderGraphEditorTool::GetSelectedGraphNodes() const {

	const int32_t selectedCount =
		ed::GetSelectedNodes(nullptr, 0);
	if (selectedCount <= 0) {
		return {};
	}

	std::vector<ed::NodeId> selectedIDs(
		static_cast<size_t>(selectedCount));
	const int32_t writtenCount =
		ed::GetSelectedNodes(
			selectedIDs.data(),
			selectedCount);

	std::vector<UUID> nodes;
	nodes.reserve(static_cast<size_t>(writtenCount));
	for (int32_t index = 0;
		index < writtenCount; ++index) {

		const UUID nodeID{
			static_cast<uint64_t>(
				selectedIDs[index].Get())
		};
		const auto found = std::find_if(
			graph_.nodes.begin(),
			graph_.nodes.end(),
			[&](const ShaderGraphNode& node) {
				return node.id == nodeID;
			});
		if (found != graph_.nodes.end()) {
			nodes.emplace_back(nodeID);
		}
	}
	return nodes;
}

void Engine::ShaderGraphEditorTool::GroupSelectedNodes() {

	const std::vector<UUID> selectedNodes =
		GetSelectedGraphNodes();
	if (selectedNodes.size() < 2) {
		return;
	}

	Vector2 minimum{
		(std::numeric_limits<float>::max)(),
		(std::numeric_limits<float>::max)(),
	};
	Vector2 maximum{
		(std::numeric_limits<float>::lowest)(),
		(std::numeric_limits<float>::lowest)(),
	};
	for (UUID nodeID : selectedNodes) {
		const ed::NodeId editorNodeID(
			ToNodeEditorID(nodeID));
		const ImVec2 position =
			ed::GetNodePosition(editorNodeID);
		const ImVec2 size =
			ed::GetNodeSize(editorNodeID);
		minimum.x =
			(std::min)(minimum.x, position.x);
		minimum.y =
			(std::min)(minimum.y, position.y);
		maximum.x =
			(std::max)(maximum.x, position.x + size.x);
		maximum.y =
			(std::max)(maximum.y, position.y + size.y);

		const auto node = std::find_if(
			graph_.nodes.begin(),
			graph_.nodes.end(),
			[&](const ShaderGraphNode& value) {
				return value.id == nodeID;
			});
		if (node != graph_.nodes.end()) {
			node->position =
				Vector2(position.x, position.y);
		}
	}

	ShaderGraphGroup group{
		.id = UUID::New(),
		.name = "グループ " +
			std::to_string(graph_.groups.size() + 1),
		.position = Vector2(
			minimum.x - kGroupHorizontalPadding,
			minimum.y - kGroupTopPadding),
		.size = Vector2(
			maximum.x - minimum.x +
				kGroupHorizontalPadding * 2.0f,
			maximum.y - minimum.y +
				kGroupTopPadding +
				kGroupBottomPadding),
	};
	const ed::NodeId groupID(
		ToNodeEditorID(group.id));
	for (UUID nodeID : selectedNodes) {
		const auto node = std::find_if(
			graph_.nodes.begin(),
			graph_.nodes.end(),
			[&](const ShaderGraphNode& value) {
				return value.id == nodeID;
			});
		if (node != graph_.nodes.end()) {
			node->groupID = group.id;
		}
	}
	ed::SetNodePosition(
		groupID,
		ImVec2(
			group.position.x,
			group.position.y));
	ed::SetGroupSize(
		groupID,
		ImVec2(group.size.x, group.size.y));
	graph_.groups.emplace_back(std::move(group));

	ed::ClearSelection();
	ed::SelectNode(groupID);
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::CopySelection() {

	const std::vector<UUID> selected = GetSelectedGraphNodes();
	if (selected.empty()) {
		return;
	}
	std::unordered_set<uint64_t> selectedIDs;
	for (UUID id : selected) {
		selectedIDs.insert(id.value);
	}
	ShaderGraphAsset fragment{};
	fragment.name = "Clipboard";
	fragment.target = graph_.target;
	for (const ShaderGraphNode& node : graph_.nodes) {
		if (!selectedIDs.contains(node.id.value) ||
			node.id == graph_.outputNode ||
			node.id == graph_.vertexOutputNode) {
			continue;
		}
		fragment.nodes.emplace_back(node);
	}
	if (fragment.nodes.empty()) {
		return;
	}
	fragment.outputNode = fragment.nodes.front().id;
	for (const ShaderGraphLink& link : graph_.links) {
		if (selectedIDs.contains(link.outputNode.value) &&
			selectedIDs.contains(link.inputNode.value)) {
			fragment.links.emplace_back(link);
		}
	}
	const std::string text =
		"NEM_SHADER_GRAPH_CLIPBOARD\n" + ToJson(fragment).dump();
	ImGui::SetClipboardText(text.c_str());
	statusMessage_ = "選択ノードをコピーしました";
}

void Engine::ShaderGraphEditorTool::PasteSelection() {

	const char* clipboard = ImGui::GetClipboardText();
	if (!clipboard) {
		return;
	}
	constexpr std::string_view kHeader =
		"NEM_SHADER_GRAPH_CLIPBOARD\n";
	const std::string_view text(clipboard);
	if (!text.starts_with(kHeader)) {
		return;
	}

	const nlohmann::json data = nlohmann::json::parse(
		text.substr(kHeader.size()), nullptr, false);
	ShaderGraphAsset fragment{};
	if (data.is_discarded() || !FromJson(data, fragment)) {
		statusMessage_ = "コピーしたノードを読み込めませんでした";
		return;
	}

	std::unordered_map<uint64_t, UUID> idMap;
	ed::ClearSelection();
	for (ShaderGraphNode& node : fragment.nodes) {
		const UUID sourceID = node.id;
		node.id = UUID::New();
		node.groupID = UUID{};
		idMap[sourceID.value] = node.id;
		node.position.x += 32.0f;
		node.position.y += 32.0f;
		for (ShaderGraphPort& port : node.inputPorts) {
			port.id = UUID::New();
		}
		for (ShaderGraphPort& port : node.outputPorts) {
			port.id = UUID::New();
		}
		const UUID nodeID = node.id;
		const Vector2 position = node.position;
		graph_.nodes.emplace_back(std::move(node));
		ed::SetNodePosition(
			ed::NodeId(ToNodeEditorID(nodeID)),
			ImVec2(position.x, position.y));
		ed::SelectNode(
			ed::NodeId(ToNodeEditorID(nodeID)), true);
	}
	for (ShaderGraphLink& link : fragment.links) {
		const auto source = idMap.find(link.outputNode.value);
		const auto destination = idMap.find(link.inputNode.value);
		if (source == idMap.end() || destination == idMap.end()) {
			continue;
		}
		link.id = UUID::New();
		link.outputNode = source->second;
		link.inputNode = destination->second;
		graph_.links.emplace_back(std::move(link));
	}
	graphDirty_ = true;
	CommitGraphHistory();
	statusMessage_ = "ノードを貼り付けました";
}

void Engine::ShaderGraphEditorTool::UndoGraph() {

	CommitGraphHistory();
	if (!history_.Undo(graph_)) {
		return;
	}
	InvalidateNodePreviews();
	graphDirty_ = true;
	previewCompileDirty_ =
		MakeShaderGraphCompileState(graph_) !=
		compiledGraphState_;
	restoreNodePositions_ = true;
	editingGroup_ = UUID{};
	contextNode_ = UUID{};
	selectedParameter_ = -1;
	ResetNodeEditor();
	statusMessage_ = "編集を元に戻しました";
}

void Engine::ShaderGraphEditorTool::RedoGraph() {

	if (!history_.Redo(graph_)) {
		return;
	}
	InvalidateNodePreviews();
	graphDirty_ = true;
	previewCompileDirty_ =
		MakeShaderGraphCompileState(graph_) !=
		compiledGraphState_;
	restoreNodePositions_ = true;
	editingGroup_ = UUID{};
	contextNode_ = UUID{};
	selectedParameter_ = -1;
	ResetNodeEditor();
	statusMessage_ = "編集をやり直しました";
}

void Engine::ShaderGraphEditorTool::CommitGraphHistory() {

	CaptureNodePositions();
	if (!history_.Commit(graph_)) {
		return;
	}
	graphDirty_ = true;
	previewCompileDirty_ =
		MakeShaderGraphCompileState(graph_) !=
		compiledGraphState_;
}

void Engine::ShaderGraphEditorTool::AddNode(
	ShaderGraphNodeKind kind, Vector2 position) {

	if (kind == ShaderGraphNodeKind::VertexOutput &&
		graph_.vertexOutputNode) {

		return;
	}

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = kind,
		.position = position,
	};
	if (kind == ShaderGraphNodeKind::Constant) {
		node.value =
			DefaultValueForGraphType(node.valueType);
	} else if (
		kind ==
		ShaderGraphNodeKind::TextureSample) {

		node.value.value = Color4::White();
	} else if (kind == ShaderGraphNodeKind::SamplerState) {

		node.sampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.sampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.sampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.previewExpanded = false;
	} else if (kind == ShaderGraphNodeKind::CustomFunction) {
		const std::string functionName =
			"CustomFunction_" + ToString(node.id);
		node.functionName = functionName;
		node.inputPorts.emplace_back(ShaderGraphPort{
			.id = UUID::New(),
			.name = "Input",
			.type = ShaderGraphValueType::Float,
			.defaultValue = DefaultValueForGraphType(
				ShaderGraphValueType::Float),
			});
		node.outputPorts.emplace_back(ShaderGraphPort{
			.id = UUID::New(),
			.name = "Output",
			.type = ShaderGraphValueType::Float,
			.defaultValue = DefaultValueForGraphType(
				ShaderGraphValueType::Float),
			});
		node.functionBody =
			"void " + functionName +
			"(float Input, out float Output) {\n"
			"\tOutput = Input;\n"
			"}";
	}
	const UUID nodeID = node.id;
	graph_.nodes.emplace_back(std::move(node));
	if (kind == ShaderGraphNodeKind::VertexOutput) {
		graph_.vertexOutputNode = nodeID;
	}
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::AddConstantNode(
	ShaderGraphValueType type,
	Vector2 position) {

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Constant,
		.valueType = type,
		.value = DefaultValueForGraphType(type),
		.position = position,
		.previewExpanded = false,
	};
	const UUID nodeID = node.id;
	graph_.nodes.emplace_back(std::move(node));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::AddParameterNode(
	UUID parameterID, Vector2 position) {

	const auto parameter = std::find_if(
		graph_.parameters.begin(),
		graph_.parameters.end(),
		[&](const ShaderGraphParameter& value) {
			return value.id == parameterID;
		});
	if (parameter == graph_.parameters.end()) {
		return;
	}
	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Parameter,
		.parameterID = parameterID,
		.valueType = parameter->type,
		.position = position,
		.previewExpanded = false,
	};
	const UUID nodeID = node.id;
	graph_.nodes.emplace_back(std::move(node));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::AddKeywordNode(
	UUID keywordID, Vector2 position) {

	const auto keyword = std::find_if(
		graph_.keywords.begin(), graph_.keywords.end(),
		[&](const ShaderGraphKeyword& value) {
			return value.id == keywordID;
		});
	if (keyword == graph_.keywords.end()) {
		return;
	}

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Keyword,
		.valueType = keyword->type == ShaderGraphKeywordType::Boolean ?
			ShaderGraphValueType::Boolean : ShaderGraphValueType::Integer,
		.position = position,
		.keywordID = keywordID,
		.previewExpanded = false,
	};
	const UUID nodeID = node.id;
	graph_.nodes.emplace_back(std::move(node));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	graphDirty_ = true;
}
