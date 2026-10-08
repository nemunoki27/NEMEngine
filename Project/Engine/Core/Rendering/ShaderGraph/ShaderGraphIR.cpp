#include "ShaderGraphIR.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

// c++
#include <algorithm>
#include <unordered_set>

namespace {

	using namespace Engine;

	// 入力先の識別子とピン番号
	struct InputKey {

		uint64_t node = 0;
		uint32_t port = 0;

		bool operator==(const InputKey&) const = default;
	};

	// 入力先の索引ハッシュ
	struct InputKeyHasher {

		size_t operator()(const InputKey& key) const noexcept {

			return std::hash<uint64_t>{}(key.node ^ (static_cast<uint64_t>(key.port) << 32));
		}
	};

	// IRの索引と走査状態を保持する
	class IRBuildContext {
	public:

		// 検証と走査に使う接続索引を作る
		IRBuildContext(const ShaderGraphAsset& graph, ShaderGraphIRModule& module) :
			graph_(graph), module_(module) {

			for (const ShaderGraphNode& node : graph.nodes) {
				nodes_[node.id.value] = &node;
			}
			for (const ShaderGraphLink& link : graph.links) {
				incoming_[InputKey{
					.node = link.inputNode.value,
					.port = link.inputSlot,
					}] = &link;
			}
		}

		// 出力から辿ってステージ別の評価順を作る
		void BuildStage(Engine::UUID root, ShaderGraphStage stage, std::vector<ShaderGraphIRInstruction>& out) {

			if (!root) {
				return;
			}
			visiting_.clear();
			visited_.clear();
			Visit(root, stage, out);
		}

		// 未使用の公開値もIDの衝突を確認する
		void ValidatePublicIdentifiers() {

			// Materialへ渡す公開値は共通のID集合で確認する
			std::unordered_set<uint64_t> identifiers;
			ValidateIdentifiers(graph_.parameters, "パラメータIDが未設定か重複しています", identifiers);
			ValidateIdentifiers(graph_.keywords, "キーワードIDが未設定か重複しています", identifiers);
		}

		// IDと接続先の整合性を確認する
		void ValidateStructure() {

			ValidatePublicIdentifiers();

			std::unordered_set<uint64_t> nodeIDs;
			for (const ShaderGraphNode& node : graph_.nodes) {
				if (!node.id || !nodeIDs.insert(node.id.value).second) {
					AddDiagnostic(node.id, ShaderGraphStage::Any,
						UINT32_MAX, "ノードIDが未設定か重複しています");
				}
			}

			std::unordered_set<uint64_t> linkIDs;
			std::unordered_set<InputKey, InputKeyHasher> destinations;
			for (const ShaderGraphLink& link : graph_.links) {
				if (!link.id || !linkIDs.insert(link.id.value).second) {
					AddDiagnostic(link.inputNode, ShaderGraphStage::Any,
						link.inputSlot, "リンクIDが未設定か重複しています");
				}

				const ShaderGraphNode* source = FindNode(link.outputNode);
				const ShaderGraphNode* destination = FindNode(link.inputNode);
				if (!source || !destination) {
					AddDiagnostic(link.inputNode, ShaderGraphStage::Any,
						link.inputSlot, "リンク先のノードが見つかりません");
					continue;
				}
				if (link.outputSlot >= GetShaderGraphOutputCount(*source)) {
					AddDiagnostic(link.outputNode, ShaderGraphStage::Any,
						link.outputSlot, "リンク元の出力ピンが範囲外です");
				}
				if (link.inputSlot >= GetShaderGraphInputCount(*destination)) {
					AddDiagnostic(link.inputNode, ShaderGraphStage::Any,
						link.inputSlot, "リンク先の入力ピンが範囲外です");
				}
				const InputKey destinationKey{
					.node = link.inputNode.value,
					.port = link.inputSlot,
				};
				if (!destinations.insert(destinationKey).second) {
					AddDiagnostic(link.inputNode, ShaderGraphStage::Any,
						link.inputSlot, "1つの入力ピンに複数のリンクがあります");
				}
			}
		}

	private:

		//--------- variables ----------------------------------------------------

		const ShaderGraphAsset& graph_; // 検証対象
		ShaderGraphIRModule& module_; // 検証と走査の結果
		std::unordered_map<uint64_t, const ShaderGraphNode*> nodes_; // ノードの索引
		std::unordered_map<InputKey, const ShaderGraphLink*, InputKeyHasher> incoming_; // 入力接続の索引
		std::unordered_set<uint64_t> visiting_; // 走査中のノード
		std::unordered_set<uint64_t> visited_; // 走査済みのノード

		//--------- functions ----------------------------------------------------

		// 公開値のIDが有効か確認する
		template<typename T>
		void ValidateIdentifiers(const std::vector<T>& values, const char* message,
			std::unordered_set<uint64_t>& identifiers) {

			for (const T& value : values) {
				if (!value.id || !identifiers.insert(value.id.value).second) {
					AddDiagnostic({}, ShaderGraphStage::Any, UINT32_MAX, message);
				}
			}
		}

		// IDからノードを取得する
		const ShaderGraphNode* FindNode(Engine::UUID id) const {

			const auto found = nodes_.find(id.value);
			return found != nodes_.end() ? found->second : nullptr;
		}

		// 入力を先に評価し循環を検出する
		void Visit(Engine::UUID nodeID, ShaderGraphStage stage,
			std::vector<ShaderGraphIRInstruction>& out) {

			if (visited_.contains(nodeID.value)) {
				return;
			}
			if (!visiting_.insert(nodeID.value).second) {
				AddDiagnostic(nodeID, stage, UINT32_MAX,
					"ノード接続が循環しています");
				return;
			}

			const ShaderGraphNode* node = FindNode(nodeID);
			if (!node) {
				AddDiagnostic(nodeID, stage, UINT32_MAX,
					"評価対象のノードが見つかりません");
				visiting_.erase(nodeID.value);
				return;
			}

			const ShaderGraphNodeDescriptor* descriptor =
				ShaderGraphNodeRegistry::Find(node->kind);
			const ShaderGraphStage nodeStage =
				node->stage != ShaderGraphStage::Any ?
				node->stage :
				(descriptor ? descriptor->stage : ShaderGraphStage::Any);
			if (nodeStage != ShaderGraphStage::Any &&
				nodeStage != stage) {
				AddDiagnostic(nodeID, stage, UINT32_MAX,
					"別ステージ専用のノードが接続されています");
			}

			ShaderGraphIRInstruction instruction{
				.sourceNode = node->id,
				.kind = node->kind,
				.stage = stage,
				.precision = node->precision == ShaderGraphPrecision::Inherit ?
					graph_.defaultPrecision : node->precision,
			};
			for (uint32_t port = 0;
				port < GetShaderGraphInputCount(*node); ++port) {

				const auto found = incoming_.find(InputKey{
					.node = node->id.value,
					.port = port,
					});
				if (found == incoming_.end()) {
					continue;
				}
				const ShaderGraphLink& link = *found->second;
				Visit(link.outputNode, stage, out);
				instruction.inputs.emplace_back(ShaderGraphIRInput{
					.sourceNode = link.outputNode,
					.sourcePort = link.outputSlot,
					.destinationPort = link.inputSlot,
					});
			}

			visiting_.erase(nodeID.value);
			visited_.insert(nodeID.value);
			out.emplace_back(std::move(instruction));
		}

		// 構造とステージの検証結果を追加する
		void AddDiagnostic(Engine::UUID node, ShaderGraphStage stage,
			uint32_t port, std::string message) {

			module_.diagnostics.emplace_back(ShaderGraphDiagnostic{
				.severity = ShaderGraphDiagnosticSeverity::Error,
				.stage = stage,
				.node = node,
				.port = port,
				.message = std::move(message),
				});
		}

	};
}

//============================================================================
//	ShaderGraphIRModule classMethods
//============================================================================
bool Engine::ShaderGraphIRModule::Succeeded() const {

	return std::none_of(diagnostics.begin(), diagnostics.end(),
		[](const ShaderGraphDiagnostic& diagnostic) {
			return diagnostic.severity == ShaderGraphDiagnosticSeverity::Error;
		});
}

//============================================================================
//	ShaderGraphIRBuilder classMethods
//============================================================================
Engine::ShaderGraphIRModule Engine::ShaderGraphIRBuilder::Build(const ShaderGraphAsset& graph) {

	ShaderGraphIRModule module{};
	IRBuildContext context(graph, module);
	// 全体の構造検証から各出力の走査へ進む
	context.ValidateStructure();
	if (graph.domain == ShaderGraphDomain::Surface) {
		context.BuildStage(graph.vertexOutputNode,
			ShaderGraphStage::Vertex, module.vertexInstructions);
		context.BuildStage(graph.outputNode,
			ShaderGraphStage::Fragment, module.fragmentInstructions);
	} else if (graph.domain == ShaderGraphDomain::PostProcess) {
		context.BuildStage(graph.outputNode,
			ShaderGraphStage::Compute, module.computeInstructions);
	} else {
		context.BuildStage(graph.outputNode,
			ShaderGraphStage::RayGeneration,
			module.rayGenerationInstructions);
	}
	return module;
}

bool Engine::ShaderGraphIRBuilder::ValidatePublicIdentifiers(const ShaderGraphAsset& graph,
	std::vector<ShaderGraphDiagnostic>& diagnostics) {

	// 展開で失われる公開値を先に検証する
	ShaderGraphIRModule module{};
	IRBuildContext context(graph, module);
	context.ValidatePublicIdentifiers();
	const bool succeeded = module.Succeeded();
	diagnostics.insert(diagnostics.end(), module.diagnostics.begin(), module.diagnostics.end());
	return succeeded;
}
