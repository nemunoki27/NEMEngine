#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCompiler.h"
#include "ShaderGraphSourceUtility.h"

// c++
#include <unordered_map>
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	ShaderGraphExpressionCompiler class
	//	Nodeの接続を辿り値と宣言を生成する
	//============================================================================
	class ShaderGraphExpressionCompiler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Graphの索引と接続を構築する
		ShaderGraphExpressionCompiler(const ShaderGraphAsset& graph, ShaderGraphCompileOutput& output);
		// 接続値を入力の型へ変換する
		ShaderGraphExpression EmitInput(
			const ShaderGraphNode& node, uint32_t slot, ShaderGraphValueType target, std::string_view fallback);
		// 循環を確認しNodeの値を生成する
		ShaderGraphExpression EmitNode(Engine::UUID nodeID, uint32_t outputSlot);
		// 公開値の構造体を生成する
		std::string BuildParameterStructure() const;
		// Samplerの宣言を生成する
		std::string BuildSamplerDeclarations() const;
		// Material定数の宣言を生成する
		std::string BuildMaterialConstantBuffer(
			uint32_t bindPoint = 3, std::string_view bufferName = "MaterialParameters") const;
		// Material値の取得関数を生成する
		std::string BuildMaterialParameterGetter() const;
		// 外部関数の宣言を生成する
		std::string BuildCustomFunctionDeclarations() const;
		// 生成時の診断を追加する
		void AddDiagnostic(Engine::UUID node, std::string message);

		//--------- accessor -----------------------------------------------------

		// 識別子からNodeを取得する
		const ShaderGraphNode* FindNode(Engine::UUID id) const;
		// 値の評価文を取得する
		const std::string& GetEvaluationStatements() const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct EndpointKey {

			uint64_t node = 0;
			uint32_t slot = 0;

			bool operator==(const EndpointKey&) const = default;
		};

		struct EndpointKeyHasher {

			size_t operator()(const EndpointKey& key) const noexcept;
		};

		//--------- variables ----------------------------------------------------

		const ShaderGraphAsset& graph_;
		ShaderGraphCompileOutput& output_;
		std::unordered_map<uint64_t, const ShaderGraphNode*> nodes_;
		std::unordered_map<uint64_t, const ShaderGraphParameter*> parameters_;
		std::unordered_map<uint64_t, const ShaderGraphKeyword*> keywords_;
		std::unordered_map<uint64_t, std::string> parameterFields_;
		std::unordered_map<uint64_t, std::string> keywordFields_;
		std::unordered_map<uint64_t, std::string> samplerNames_;
		std::unordered_map<EndpointKey, const ShaderGraphLink*, EndpointKeyHasher> incoming_;
		// ノードと出力ピンの組を保持し、共有式の再生成を避ける
		std::unordered_map<EndpointKey, ShaderGraphExpression, EndpointKeyHasher> cache_;
		std::unordered_map<uint64_t, std::string> textureSampleVariables_;
		std::unordered_map<uint64_t, std::vector<ShaderGraphExpression>> customFunctionOutputs_;
		std::unordered_set<uint64_t> visiting_;
		std::string evaluationStatements_;

		//--------- functions ----------------------------------------------------

		// Nodeの用途に応じて式の生成先を選ぶ
		ShaderGraphExpression EmitNodeExpression(const ShaderGraphNode& node, uint32_t outputSlot);
		// 公開値と組込み入力の式を作る
		ShaderGraphExpression EmitValueNode(const ShaderGraphNode& node, uint32_t outputSlot);
		// 数値演算の式を作る
		ShaderGraphExpression EmitMathNode(const ShaderGraphNode& node);
		// 座標変換と成分操作の式を作る
		ShaderGraphExpression EmitCoordinatesNode(const ShaderGraphNode& node, uint32_t outputSlot);
		// Textureの参照とサンプル式を作る
		ShaderGraphExpression EmitTextureNode(const ShaderGraphNode& node, uint32_t outputSlot);
		// 描画結果とRayの参照式を作る
		ShaderGraphExpression EmitSceneNode(const ShaderGraphNode& node, uint32_t outputSlot);
		// 外部関数と出力Nodeの診断を作る
		ShaderGraphExpression EmitCustomNode(const ShaderGraphNode& node, uint32_t outputSlot);
		// 実行時Keywordの宣言を追加する
		void AppendKeywordFields(std::string& source) const;
		// 公開値とKeywordの宣言を追加する
		void AppendParameterFields(std::string& source) const;
		// 接続値を元の型で取得する
		ShaderGraphExpression EmitDynamicInput(const ShaderGraphNode& node, uint32_t slot, std::string_view fallback);
		// 二項演算の式を作る
		ShaderGraphExpression EmitBinary(const ShaderGraphNode& node, std::string_view operation);
	};
} // Engine
