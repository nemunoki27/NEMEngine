#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphParameterDefaults.h"

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	void BuildParameterBindings(const ShaderGraphAsset& graph, ShaderGraphCompileOutput& output) {

		// Materialに公開するParameter情報を作る
		for (const ShaderGraphParameter& parameter : graph.parameters) {

			const auto valueType = parameter.type == ShaderGraphValueType::Integer ? D3D_SVT_INT :
				(parameter.type == ShaderGraphValueType::Boolean || parameter.type == ShaderGraphValueType::Texture2D ?
					D3D_SVT_UINT : D3D_SVT_FLOAT);
			output.parameters.emplace_back(ShaderParameterMetadata{
				.shaderName = MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName,
					parameter.id),
				.displayName = parameter.name,
				.id = MaterialParameterID::FromUUID(parameter.id),
				.semantic = parameter.semantic,
				.isColor = parameter.type == ShaderGraphValueType::Color,
				.isTexture = parameter.type == ShaderGraphValueType::Texture2D,
				.exposed = parameter.exposed && parameter.scope != ShaderGraphParameterScope::Global,
				.valueType = valueType,
				.componentCount = (std::max)(1u, ComponentCount(parameter.type)),
			});
		}
		// 実行時切替のKeywordだけをParameterとして公開する
		for (const ShaderGraphKeyword& keyword : graph.keywords) {
			if (!keyword.runtimeToggle) {
				continue;
			}
			output.parameters.emplace_back(ShaderParameterMetadata{
				.shaderName = MakeIdentifier(keyword.referenceName.empty() ? keyword.name : keyword.referenceName, keyword.id),
				.displayName = keyword.name,
				.id = MaterialParameterID::FromUUID(keyword.id),
				.semantic = MaterialParameterSemantic::None,
				.isColor = false,
				.isTexture = false,
				.valueType = keyword.type == ShaderGraphKeywordType::Boolean ? D3D_SVT_UINT : D3D_SVT_INT,
				.componentCount = 1,
			});
		}
		// 展開後の初期値を生成Materialへ渡す
		output.defaultParameters = ShaderGraphParameterDefaults::Build(graph);
	}
} // Engine::ShaderGraphStageSource
