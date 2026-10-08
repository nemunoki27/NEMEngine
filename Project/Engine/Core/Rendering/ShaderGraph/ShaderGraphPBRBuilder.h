#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	ShaderGraphPBRBuilder class
	//	標準PBRの設定を編集可能なノードへ組み立てる
	//============================================================================
	class ShaderGraphPBRBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 標準値とMaterial設定から候補Graphを作成する
		static ShaderGraphAsset Build(
			const MaterialAsset& material, const MaterialAsset& defaults, const PipelineStaticSamplerSettings& sampler);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// ノード上の出力接続点
		struct Pin {

			UUID node{};	   // 接続元のノード
			uint32_t slot = 0; // 接続元の出力番号
		};

		//--------- variables ----------------------------------------------------

		ShaderGraphAsset graph_;				   // 組み立て中のGraph
		MaterialParameterSet values_;			   // 標準値とMaterial設定の合成
		std::unordered_set<std::string> consumed_; // 取り込んだ設定名
		UUID group_{};							   // 現在の配置領域
		Pin sampler_{};							   // 共通サンプラーの接続点
		float row_ = 0.0f;						   // 配置領域の行位置
		float column_ = 0.0f;					   // 次のノードの列位置

		//--------- functions ----------------------------------------------------

		// 組み立て中の状態を初期化する
		ShaderGraphPBRBuilder() = default;

		// ノードを配置して接続点を返す
		Pin Node(ShaderGraphNodeKind kind);
		// 出力と入力の接続を追加する
		void Link(Pin from, Pin to, uint32_t input);
		// 数値の定数ノードを追加する
		Pin Constant(float value);
		// 二つの入力を演算ノードへ接続する
		Pin Operation(ShaderGraphNodeKind kind, Pin a, Pin b);
		// 名前と意味から設定値を解決する
		MaterialParameterValue Value(const std::string& name, MaterialParameterValue fallback);
		// 同じ名前の入力を再利用して設定値を登録する
		Pin Parameter(const std::string& name, ShaderGraphValueType type, MaterialParameterValue fallback);
		// 画像入力とサンプラーと代替値を接続する
		Pin Texture(const std::string& name, uint32_t slot, bool normal = false);
		// 用途別のノード配置領域を追加する
		void Begin(const std::string& name);
	};
}
