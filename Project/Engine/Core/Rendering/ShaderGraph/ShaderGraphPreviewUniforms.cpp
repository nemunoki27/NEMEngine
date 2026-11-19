#include "ShaderGraphPreviewUniforms.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphSourceUtility.h"
#include "ShaderGraphNodeRegistry.h"

// c++
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace {

	using namespace Engine;
	using namespace Engine::ShaderGraphSourceUtility;
	using Type = ShaderGraphValueType;
	using Kind = ShaderGraphNodeKind;

	// プレビュー内だけで使う識別子と転送値を生成する
	class UniformBuilder {
	public:

		explicit UniformBuilder(ShaderGraphAsset& graph) : graph_(graph), identifiers_(CollectIdentifiers(graph)) {

			for (const auto& node : graph.nodes) {
				if (!node.functionName.empty()) { functionNames_.insert(node.functionName); }
			}
		}

		// 入力用のParameterと接続を作る
		void AddInput(ShaderGraphNode& node, Type type, const MaterialParameterValue& value) {

			const auto slot = static_cast<uint32_t>(node.inputPorts.size());
			node.inputPorts.push_back({ MakeID(node.id, 0x300u + slot), "input" + std::to_string(slot), type, value });
			ConnectValue(node, slot, type, value);
		}

		// 汎用ノードの未接続数値も同じ定数領域へ移す
		void AddDefaults(const ShaderGraphNode& node) {

			if (node.kind < Kind::Exponential && node.kind != Kind::CustomFunction) { return; }
			for (uint32_t slot = 0; slot < GetShaderGraphInputCount(node); ++slot) {
				const bool connected = std::any_of(graph_.links.begin(), graph_.links.end(),
					[&](const ShaderGraphLink& link) { return link.inputNode == node.id && link.inputSlot == slot; });
				if (connected) { continue; }
				auto defaults = GetShaderGraphInputDefault(node, slot);
				if (slot < node.inputPorts.size() &&
					(node.inputPorts[slot].hasDefault || node.kind == Kind::CustomFunction)) {
					defaults = { node.inputPorts[slot].type, node.inputPorts[slot].defaultValue, {} };
				}
				if (defaults.binding.empty() && IsNumeric(defaults.type)) {
					ConnectValue(node, slot, defaults.type, defaults.value);
				}
			}
		}

		// 通常の定数をParameterへ置き換える
		void AddConstant(ShaderGraphNode& node) {

			if (node.valueType == Type::SamplerState || node.valueType == Type::Invalid) { return; }
			const auto parameterID = MakeID(node.id, 0x100u);
			ShaderGraphParameter parameter{};
			parameter.id = parameterID;
			parameter.name = "PreviewConstant";
			parameter.referenceName = "PreviewConstant";
			parameter.type = node.valueType;
			parameter.defaultValue = node.value;
			parameter.exposed = true;
			graph_.parameters.emplace_back(std::move(parameter));
			node.kind = Kind::Parameter;
			node.parameterID = parameterID;
		}

		// 行列を行ごとの転送値から組み立てる
		void AddMatrix(ShaderGraphNode& node, Type type) {

			const uint32_t width = type == Type::Matrix2 ? 2u : (type == Type::Matrix3 ? 3u : 4u);
			const auto rows = node.settings.matrixRows;
			const int32_t axis = static_cast<int32_t>(node.settings.matrixAxis);
			const auto functionName = FunctionName("NEMPreviewMatrix" + std::to_string(width));
			ResetFunction(node, type, functionName);
			for (uint32_t i = 0; i < width; ++i) { AddInput(node, Type::Float4, MaterialParameterValue{ rows[i] }); }
			AddInput(node, Type::Integer, MaterialParameterValue{ axis });

			std::string code = FunctionDeclaration(node);
			code += "\tvalue = " + HLSLType(type) + "(";
			for (uint32_t i = 0; i < width; ++i) {
				if (i != 0) { code += ", "; }
				code += "input" + std::to_string(i) + "." + (width == 2 ? "xy" : (width == 3 ? "xyz" : "xyzw"));
			}
			code += ");\n\tif (input" + std::to_string(width) + " != 0) { value = transpose(value); }\n}\n";
			node.functionBody = std::move(code);
		}

		// グラデーションの全スロットを転送値から組み立てる
		void AddGradient(ShaderGraphNode& node) {

			auto colors = node.settings.gradientColors;
			auto alphas = node.settings.gradientAlphas;
			if (colors.empty()) { colors = { Vector4{ 0, 0, 0, 0 }, Vector4{ 1, 1, 1, 1 } }; }
			if (alphas.empty()) { alphas = { Vector2{ 1, 0 }, Vector2{ 1, 1 } }; }
			std::stable_sort(colors.begin(), colors.end(), [](const Vector4& a, const Vector4& b) { return a.w < b.w; });
			std::stable_sort(alphas.begin(), alphas.end(), [](const Vector2& a, const Vector2& b) { return a.y < b.y; });
			const auto colorCount = static_cast<int32_t>(std::min(colors.size(), size_t{ 8 }));
			const auto alphaCount = static_cast<int32_t>(std::min(alphas.size(), size_t{ 8 }));
			const auto mode = static_cast<int32_t>(node.settings.gradientMode);
			const auto functionName = FunctionName("NEMPreviewGradient");
			ResetFunction(node, Type::Gradient, functionName);
			for (size_t i = 0; i < 8; ++i) {
				AddInput(node, Type::Float4, MaterialParameterValue{ i < colors.size() ? colors[i] : Vector4{} });
			}
			for (size_t i = 0; i < 8; ++i) {
				AddInput(node, Type::Float2, MaterialParameterValue{ i < alphas.size() ? alphas[i] : Vector2{} });
			}
			AddInput(node, Type::Integer, MaterialParameterValue{ colorCount });
			AddInput(node, Type::Integer, MaterialParameterValue{ alphaCount });
			AddInput(node, Type::Integer, MaterialParameterValue{ mode });
			std::string code = FunctionDeclaration(node);
			code += "\tvalue = ShaderGraphDefaultGradient();\n"
				"\tvalue.colorCount = (uint)input16;\n\tvalue.alphaCount = (uint)input17;\n\tvalue.fixedMode = (uint)input18;\n";
			for (uint32_t i = 0; i < 8; ++i) {
				code += "\tvalue.colors[" + std::to_string(i) + "] = input" + std::to_string(i) + ";\n";
				code += "\tvalue.alphas[" + std::to_string(i) + "] = input" + std::to_string(i + 8) + ";\n";
			}
			code += "}\n";
			node.functionBody = std::move(code);
		}

		// 新しい入力ノードをグラフへ追加する
		void Finish() {

			graph_.nodes.insert(graph_.nodes.end(), std::make_move_iterator(nodes_.begin()), std::make_move_iterator(nodes_.end()));
		}

	private:

		ShaderGraphAsset& graph_;
		std::unordered_set<uint64_t> identifiers_;
		std::unordered_set<std::string> functionNames_;
		std::vector<ShaderGraphNode> nodes_;

		// 転送値を読み込むParameterを入力へ接続する
		void ConnectValue(const ShaderGraphNode& node, uint32_t slot, Type type, const MaterialParameterValue& value) {

			const auto parameterID = MakeID(node.id, 0x100u + slot);
			const auto sourceID = MakeID(node.id, 0x200u + slot);
			ShaderGraphParameter parameter{};
			parameter.id = parameterID;
			parameter.name = "PreviewValue";
			parameter.referenceName = "PreviewValue";
			parameter.type = type;
			parameter.defaultValue = value;
			parameter.exposed = true;
			graph_.parameters.emplace_back(std::move(parameter));
			ShaderGraphNode source{};
			source.id = sourceID;
			source.kind = Kind::Parameter;
			source.parameterID = parameterID;
			source.valueType = type;
			nodes_.emplace_back(std::move(source));
			graph_.links.push_back({ MakeID(node.id, 0x400u + slot), sourceID, 0, node.id, slot });
		}

		// 元の識別子と用途から衝突しない識別子を作る
		Engine::UUID MakeID(Engine::UUID owner, uint32_t channel) {

			return MakeUniqueID(identifiers_, 0x4E454D5072657669ull, owner, channel);
		}

		// 既存の外部関数と重ならない共有名を作る
		std::string FunctionName(const std::string& base) const {

			std::string result = base;
			uint32_t suffix = 0;
			while (functionNames_.contains(result)) { result = base + "_" + std::to_string(++suffix); }
			return result;
		}

		// 値の組み立てだけを行う関数へ変更する
		void ResetFunction(ShaderGraphNode& node, Type type, const std::string& name) {

			node.kind = Kind::CustomFunction;
			node.valueType = type;
			node.inputPorts.clear();
			node.outputPorts = { ShaderGraphPort{ MakeID(node.id, 0x500u), "value", type, {} } };
			node.customFunctionSource = ShaderGraphCustomFunctionSource::Inline;
			node.functionName = name;
			node.functionFile.clear();
			node.functionFileAsset = {};
		}

		// 転送値の型から共有関数の宣言を作る
		std::string FunctionDeclaration(const ShaderGraphNode& node) const {

			std::string result = "void " + node.functionName + "(";
			for (const auto& port : node.inputPorts) {
				result += HLSLType(port.type) + " " + port.name + ", ";
			}
			result += "out " + HLSLType(node.valueType) + " value) {\n";
			return result;
		}
	};
}

void Engine::ShaderGraphPreviewUniforms::Lift(ShaderGraphAsset& graph) {

	for (auto& parameter : graph.parameters) {
		parameter.exposed = true;
		parameter.scope = ShaderGraphParameterScope::PerMaterial;
	}
	UniformBuilder builder(graph);
	for (auto& node : graph.nodes) {
		const auto type = node.kind == Kind::Constant ? node.valueType :
			(node.kind == Kind::Matrix2 ? Type::Matrix2 : (node.kind == Kind::Matrix3 ? Type::Matrix3 :
				(node.kind == Kind::Matrix4 ? Type::Matrix4 : (node.kind == Kind::Gradient ? Type::Gradient : Type::Invalid))));
		if (type == Type::Matrix2 || type == Type::Matrix3 || type == Type::Matrix4) {
			builder.AddMatrix(node, type);
		} else if (type == Type::Gradient) {
			builder.AddGradient(node);
		} else if (node.kind == Kind::Constant) {
			builder.AddConstant(node);
		}
		builder.AddDefaults(node);
	}
	builder.Finish();
}
