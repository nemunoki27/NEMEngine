#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/Stage/ShaderReflection.h>

// c++
#include <algorithm>

namespace Engine {

	//============================================================================
	//	AutoInputLayoutBuilder structures
	//============================================================================

	// 入力レイアウトの生成結果
	struct InputLayoutBuildResult {

		// セマンティック名のリスト
		std::vector<std::string> semanticNames;
		// 入力要素
		std::vector<D3D12_INPUT_ELEMENT_DESC> elements;

		// D3D12_INPUT_LAYOUT_DESCへ変換
		D3D12_INPUT_LAYOUT_DESC GetDesc() const;
	};

	//============================================================================
	//	AutoInputLayoutBuilder class
	//	自動で入力レイアウトを生成するクラス
	//============================================================================
	class AutoInputLayoutBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AutoInputLayoutBuilder() = default;
		~AutoInputLayoutBuilder() = default;

		// リフレクション情報から入力レイアウトを自動生成する
		InputLayoutBuildResult Build(const CompiledShader& shader) const;
	};
} // Engine