#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	ShaderGraphNodeRegistry structures
	//============================================================================
	struct ShaderGraphPortDescriptor {

		std::string_view name;
		ShaderGraphValueType type = ShaderGraphValueType::Invalid;
	};

	struct ShaderGraphNodeDescriptor {

		ShaderGraphNodeKind kind = ShaderGraphNodeKind::Constant;
		std::string_view name;
		std::string_view category;
		ShaderGraphStage stage = ShaderGraphStage::Any;
		std::span<const ShaderGraphPortDescriptor> inputs;
		std::span<const ShaderGraphPortDescriptor> outputs;
		bool dynamicPorts = false;
		bool preview = true;
		bool preview3D = false;
	};

	// 未接続入力の値と組込み入力
	struct ShaderGraphInputDefault {

		ShaderGraphValueType type = ShaderGraphValueType::Invalid;
		MaterialParameterValue value{};
		std::string_view binding;
	};

	//============================================================================
	//	ShaderGraphNodeRegistry class
	//	ノード定義をエディターとコンパイラで共有するレジストリ
	//============================================================================
	class ShaderGraphNodeRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphNodeRegistry() = delete;
		~ShaderGraphNodeRegistry() = delete;

		// 登録済みノードを取得
		static std::span<const ShaderGraphNodeDescriptor> GetDescriptors();
		// ノード種別から定義を取得
		static const ShaderGraphNodeDescriptor* Find(ShaderGraphNodeKind kind);
	};

	// 汎用ノードの定義を取得する
	std::span<const ShaderGraphNodeDescriptor> GetGenericShaderGraphNodeDescriptors();
	// 汎用ノードの未接続入力を取得する
	ShaderGraphInputDefault GetShaderGraphInputDefault(const ShaderGraphNode& node, uint32_t slot);
} // Engine
