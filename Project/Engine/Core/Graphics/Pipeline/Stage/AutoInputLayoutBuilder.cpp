#include "AutoInputLayoutBuilder.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	AutoInputLayoutBuilder classMethods
//============================================================================

namespace {

	// マスクにセットされているビットの数を数える
	UINT CountMaskBits(BYTE mask) {
		UINT count = 0;
		if (mask & 0x1) { ++count; }
		if (mask & 0x2) { ++count; }
		if (mask & 0x4) { ++count; }
		if (mask & 0x8) { ++count; }
		return count;
	}
	// システムセマンティクスか判定する
	bool IsSystemSemantic(const std::string& name) {
		return name.rfind("SV_", 0) == 0;
	}
	// D3D_REGISTER_COMPONENT_TYPEとマスクからDXGI_FORMATを決定する
	DXGI_FORMAT ToFormat(D3D_REGISTER_COMPONENT_TYPE componentType, BYTE mask) {

		const UINT count = CountMaskBits(mask);

		// 連続成分前提
		Assert::Call(mask == 0x1 || mask == 0x3 || mask == 0x7 || mask == 0xF, "Unsupported semantic mask");

		switch (componentType) {
		case D3D_REGISTER_COMPONENT_FLOAT32:
			switch (count) {
			case 1: return DXGI_FORMAT_R32_FLOAT;
			case 2: return DXGI_FORMAT_R32G32_FLOAT;
			case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
			case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
			break;
		case D3D_REGISTER_COMPONENT_UINT32:
			switch (count) {
			case 1: return DXGI_FORMAT_R32_UINT;
			case 2: return DXGI_FORMAT_R32G32_UINT;
			case 3: return DXGI_FORMAT_R32G32B32_UINT;
			case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
			}
			break;
		case D3D_REGISTER_COMPONENT_SINT32:
			switch (count) {
			case 1: return DXGI_FORMAT_R32_SINT;
			case 2: return DXGI_FORMAT_R32G32_SINT;
			case 3: return DXGI_FORMAT_R32G32B32_SINT;
			case 4: return DXGI_FORMAT_R32G32B32A32_SINT;
			}
			break;
		}
		Assert::Call(false, "Unsupported input semantic format");
		return DXGI_FORMAT_UNKNOWN;
	}
}

D3D12_INPUT_LAYOUT_DESC InputLayoutBuildResult::GetDesc() const {

	D3D12_INPUT_LAYOUT_DESC desc{};
	desc.pInputElementDescs = elements.empty() ? nullptr : elements.data();
	desc.NumElements = static_cast<UINT>(elements.size());
	return desc;
}

InputLayoutBuildResult AutoInputLayoutBuilder::Build(const CompiledShader& shader) const {

	InputLayoutBuildResult result{};

	// 登録順ではなく、レジスタ番号の順でソートしてから処理する
	std::vector<ShaderInputSemantic> inputs = shader.reflection.inputs;
	std::sort(inputs.begin(), inputs.end(), [](const auto& a, const auto& b) {
		return a.registerIndex < b.registerIndex;
		});

	// 必要な数だけセマンティック名と入力要素のリストを確保
	result.semanticNames.reserve(inputs.size());
	result.elements.reserve(inputs.size());

	// システムセマンティクスを除外してセマンティック名のリストを構築
	for (const auto& input : inputs) {
		if (IsSystemSemantic(input.semanticName)) {
			continue;
		}
		result.semanticNames.emplace_back(input.semanticName);
	}

	size_t semanticNameIndex = 0;
	for (const auto& input : inputs) {

		// システムセマンティクスは入力レイアウトに含めない
		if (IsSystemSemantic(input.semanticName)) {
			continue;
		}

		// 入力要素を構築
		D3D12_INPUT_ELEMENT_DESC element{};
		element.SemanticName = result.semanticNames[semanticNameIndex].c_str();
		element.SemanticIndex = input.semanticIndex;
		element.Format = ToFormat(input.componentType, input.mask);
		element.InputSlot = 0;
		element.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		element.InstanceDataStepRate = 0;
		// 取得した入力セマンティクスの情報をリストに追加
		result.elements.emplace_back(element);
		++semanticNameIndex;
	}
	return result;
}