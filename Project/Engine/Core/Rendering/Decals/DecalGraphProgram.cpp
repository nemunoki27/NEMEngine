#include "DecalGraphProgram.h"

#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphIR.h>
#include <algorithm>
#include <array>
#include <optional>
#include <unordered_map>

using namespace Engine;

Vector4 Engine::DecalGraphValue(const MaterialParameterValue& parameter) {

	return std::visit([](const auto& v) -> Vector4 {
		using T = std::decay_t<decltype(v)>;
		if constexpr (std::is_arithmetic_v<T>) {
			const float f = static_cast<float>(v);
			return {f, f, f, f};
		} else if constexpr (std::is_same_v<T, Vector2>) return {v.x, v.y, 0, 1};
		else if constexpr (std::is_same_v<T, Vector3>) return {v.x, v.y, v.z, 1};
		else if constexpr (std::is_same_v<T, Vector4>) return v;
		else if constexpr (std::is_same_v<T, Color4>) return {v.r, v.g, v.b, v.a};
		else return {};
	}, parameter.value);
}

namespace {

	using Kind = ShaderGraphNodeKind;
	using Op = DecalGraphOp;

	std::optional<Op> Operation(Kind kind) {

		switch (kind) {
		case Kind::UV: return Op::UV;
		case Kind::WorldPosition: return Op::WorldPosition;
		case Kind::WorldNormal: return Op::WorldNormal;
		case Kind::TextureSample: return Op::TextureSample;
		case Kind::Split: return Op::Split;
		case Kind::Combine: return Op::Combine;
		case Kind::NormalUnpack: return Op::NormalUnpack;
		case Kind::TilingAndOffset: return Op::TilingAndOffset;
		case Kind::Add: return Op::Add;
		case Kind::Subtract: return Op::Subtract;
		case Kind::Multiply: return Op::Multiply;
		case Kind::Divide: return Op::Divide;
		case Kind::Power: return Op::Power;
		case Kind::Minimum: return Op::Minimum;
		case Kind::Maximum: return Op::Maximum;
		case Kind::OneMinus: return Op::OneMinus;
		case Kind::Lerp: return Op::Lerp;
		case Kind::Saturate: return Op::Saturate;
		case Kind::Sine: return Op::Sine;
		case Kind::Cosine: return Op::Cosine;
		case Kind::Absolute: return Op::Absolute;
		case Kind::Floor: return Op::Floor;
		case Kind::Fraction: return Op::Fraction;
		case Kind::SquareRoot: return Op::SquareRoot;
		case Kind::Negate: return Op::Negate;
		case Kind::Step: return Op::Step;
		case Kind::Smoothstep: return Op::Smoothstep;
		case Kind::Clamp: return Op::Clamp;
		case Kind::Branch: return Op::Branch;
		case Kind::Ceiling: return Op::Ceiling;
		case Kind::Round: return Op::Round;
		case Kind::Sign: return Op::Sign;
		case Kind::Truncate: return Op::Truncate;
		case Kind::Tangent: return Op::Tangent;
		case Kind::Reciprocal: return Op::Reciprocal;
		case Kind::Exponential: return Op::Exponential;
		case Kind::Log: return Op::Log;
		case Kind::Modulo: return Op::Modulo;
		case Kind::Constant:
		case Kind::Parameter: return Op::Literal;
		default: return {};
		}
	}

	struct Register {
		uint32_t index;
		Kind kind;
		bool texture;
	};
}

bool Engine::SupportsDecalGraphNode(ShaderGraphNodeKind kind) {

	return Operation(kind).has_value();
}

bool Engine::CompileDecalGraph(const ShaderGraphAsset& graph, const ShaderGraphIRModule& ir,
	DecalGraphProgram& program, std::vector<ShaderGraphDiagnostic>& diagnostics) {

	DecalGraphProgram result;
	const auto error = [&](const ShaderGraphNode& node, std::string message) {
		diagnostics.push_back({.node = node.id, .message = std::move(message)});
		return false;
	};
	const auto emit = [&](DecalGraphInstruction value) {
		result.instructions.push_back(value);
		return static_cast<uint32_t>(result.instructions.size() + 3u);
	};
	std::unordered_map<uint64_t, Register> registers;
	for (const auto& p : graph.parameters) {
		if (p.scope == ShaderGraphParameterScope::Global) {
			diagnostics.push_back({.message = "DecalではMaterialまたはInstanceのパラメータを使用してください"});
			return false;
		}
		if (p.exposed) result.exposed.push_back(MaterialParameterID::FromUUID(p.id));
	}
	for (const auto& entry : ir.fragmentInstructions) {
		const auto it = std::ranges::find(graph.nodes, entry.sourceNode, &ShaderGraphNode::id);
		if (it == graph.nodes.end()) {
			diagnostics.push_back({.node = entry.sourceNode, .message = "Decalの入力ノードが見つかりません"});
			return false;
		}
		const auto& node = *it;
		std::array<uint32_t, 6> inputs{};
		std::array<bool, 6> linked{};
		if (node.kind == Kind::TextureSample) inputs[1] = 2;
		if (node.kind == Kind::TilingAndOffset) { inputs[0] = 2; inputs[1] = 1; }
		if (node.kind == Kind::Power || node.kind == Kind::Divide || node.kind == Kind::Smoothstep) inputs[1] = 1;
		if (node.kind == Kind::Lerp) inputs[2] = 3;
		if (node.kind == Kind::NormalUnpack && entry.inputs.empty()) inputs[0] = emit({Op::Literal, 0, Vector4(0.5f, 0.5f, 1, 1)});
		if (node.kind == Kind::Clamp) inputs[2] = 1;
		if (node.kind == Kind::Combine) inputs[3] = 1;
		for (const auto& input : entry.inputs) {
			if (input.destinationPort >= inputs.size()) return error(node, "Decalの入力ピンが不正です");
			auto source = registers.find(input.sourceNode.value);
			if (source == registers.end()) return error(node, "Decalの入力が見つかりません");
			const Register reg = source->second;
			const bool textureInput = node.kind == Kind::TextureSample && input.destinationPort == 0;
			if (reg.texture != textureInput) return error(node, "Texture2Dを数値入力に接続することはできません");
			uint32_t index = reg.index;
			if (reg.kind == Kind::Split || (reg.kind == Kind::TextureSample && input.sourcePort >= 2)) {
				const auto channel = reg.kind == Kind::Split ? input.sourcePort : input.sourcePort - 2;
				index = emit({Op::Split, index, Vector4(static_cast<float>(channel), 0, 0, 0)});
			}
			inputs[input.destinationPort] = index;
			linked[input.destinationPort] = true;
		}
		for (uint32_t i = 0; i < inputs.size() && i < node.inputPorts.size(); ++i) {
			const auto& port = node.inputPorts[i];
			if (linked[i] || !port.hasDefault || port.type == ShaderGraphValueType::Texture2D ||
				port.type == ShaderGraphValueType::SamplerState) continue;
			inputs[i] = emit({Op::Literal, 0, DecalGraphValue(port.defaultValue)});
			linked[i] = true;
		}
		if (node.id == graph.outputNode) {
			if (node.kind != Kind::DecalOutput) return error(node, "Decal Surface出力が必要です");
			const uint32_t flags[]{1, 2, 4, 4, 8, 0};
			for (uint32_t i = 0; i < inputs.size(); ++i) {
				if (!linked[i]) continue;
				result.channels |= flags[i];
				emit({Op::Output, inputs[i], Vector4(static_cast<float>(i), 0, 0, 0)});
			}
			if (!result.channels) result.channels = 1;
			if (result.instructions.empty()) emit({Op::Output, 1u});
		} else {
			const auto op = Operation(node.kind);
			if (!op) return error(node, std::string(GetShaderGraphNodeName(node.kind)) + " はDecalの投影式では使用できません");
			const uint32_t packed = inputs[0] | (inputs[1] << 8) | (inputs[2] << 16) | (inputs[3] << 24);
			DecalGraphInstruction instruction{*op, packed, DecalGraphValue(node.value)};
			if (node.kind == Kind::Exponential || node.kind == Kind::Log) instruction.value.x = static_cast<float>(node.settings.mathBase);
			if (node.kind == Kind::Parameter) {
				const auto p = std::ranges::find(graph.parameters, node.parameterID, &ShaderGraphParameter::id);
				if (p == graph.parameters.end()) return error(node, "パラメータが見つかりません");
				instruction.parameter = MaterialParameterID::FromUUID(p->id);
				instruction.semantic = p->semantic;
				instruction.value = DecalGraphValue(p->defaultValue);
				if (p->type == ShaderGraphValueType::Texture2D) {
					instruction.op = Op::Texture;
					if (const auto* texture = std::get_if<AssetID>(&p->defaultValue.value)) instruction.texture = *texture;
				} else if (p->type >= ShaderGraphValueType::Matrix4 || p->type == ShaderGraphValueType::SamplerState)
					return error(node, "Decalに使用できないパラメータ型です");
			} else if (const auto* texture = std::get_if<AssetID>(&node.value.value); node.kind == Kind::Constant && texture) {
				instruction.op = Op::Texture;
				instruction.texture = *texture;
			}
			if (node.kind == Kind::TextureSample && !linked[0]) instruction.op = Op::Literal;
			registers.emplace(node.id.value, Register{node.kind == Kind::Split ? inputs[0] : emit(instruction), node.kind, instruction.op == Op::Texture});
		}
		if (result.instructions.size() + 4 > DecalGraphProgram::kMaxInstructions)
			return error(node, "Decalの投影式が60命令を超えています");
	}
	program = std::move(result);
	return true;
}

nlohmann::json Engine::WriteDecalGraph(const DecalGraphProgram& program) {

	nlohmann::json instructions = nlohmann::json::array(), exposed = nlohmann::json::array();
	for (const auto& i : program.instructions) {
		instructions.push_back({{"op", static_cast<uint32_t>(i.op)}, {"inputs", i.inputs}, {"value", i.value.ToJson()},
			{"parameter", ToString(Engine::UUID{i.parameter.value})}, {"texture", ToAssetReferenceJson(i.texture)},
			{"semantic", static_cast<uint32_t>(i.semantic)}});
	}
	for (const auto id : program.exposed) exposed.push_back(ToString(Engine::UUID{id.value}));
	return {{"version", 1}, {"channels", program.channels}, {"instructions", instructions}, {"exposed", exposed}};
}

bool Engine::ReadDecalGraph(const nlohmann::json& data, DecalGraphProgram& program) {

	try {
		if (data.value("version", 0) != 1 || !data.at("instructions").is_array() || !data.at("exposed").is_array() ||
			data.at("instructions").empty() ||
			data.at("instructions").size() + 4 > DecalGraphProgram::kMaxInstructions) return false;
		DecalGraphProgram result;
		result.channels = data.at("channels").get<uint32_t>() & 15u;
		for (const auto& value : data.at("instructions")) {
			const uint32_t op = value.at("op"), inputs = value.at("inputs");
			if (op > static_cast<uint32_t>(Op::Output)) return false;
			for (uint32_t shift = 0; shift < 32; shift += 8) {
				if (((inputs >> shift) & 255u) >= result.instructions.size() + 4) return false;
			}
			result.instructions.push_back({static_cast<Op>(op), inputs, Vector4::FromJson(value.at("value")),
				MaterialParameterID::FromUUID(FromString16Hex(value.value("parameter", ""))),
				ParseAssetID(value, "texture"), static_cast<MaterialParameterSemantic>(value.value("semantic", 0u))});
		}
		for (const auto& id : data.at("exposed")) {
			const auto uuid = TryParseUUID16Hex(id.get<std::string>());
			if (!uuid || !*uuid) return false;
			result.exposed.push_back(MaterialParameterID::FromUUID(*uuid));
		}
		program = std::move(result);
		return true;
	} catch (const nlohmann::json::exception&) {
		return false;
	}
}
