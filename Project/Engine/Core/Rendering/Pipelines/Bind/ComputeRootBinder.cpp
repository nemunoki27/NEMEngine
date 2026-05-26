#include "ComputeRootBinder.h"

//============================================================================
//	ComputeRootBinder classMethods
//============================================================================

void Engine::ComputeRootBinder::Bind(ID3D12GraphicsCommandList* commandList,
	const std::span<const ComputeBindItem>& items) const {

	for (const ComputeBindItem& item : items) {

		const RootBindingLocation* binding = ResolveBinding(item);
		if (!binding) {
			continue;
		}
		// バインドの種類に応じてコマンドリストにバインド
		switch (item.type) {
		case ComputeBindValueType::CBV:

			RootBindingCommand::SetComputeCBV(commandList, binding, item.gpuAddress);
			break;
		case ComputeBindValueType::SRV:

			RootBindingCommand::SetComputeSRV(commandList, binding, item.gpuAddress, item.descriptor);
			break;
		case ComputeBindValueType::UAV:

			RootBindingCommand::SetComputeUAV(commandList, binding, item.gpuAddress, item.descriptor);
			break;
		case ComputeBindValueType::AccelStruct:

			RootBindingCommand::SetComputeSRV(commandList, binding, item.gpuAddress, {});
			break;
		}
	}
}

const Engine::RootBindingLocation* Engine::ComputeRootBinder::ResolveBinding(const ComputeBindItem& item) const {
	
	// バインドの種類を取得
	ShaderBindingKind kind{};
	switch (item.type) {
	case ComputeBindValueType::CBV:

		kind = ShaderBindingKind::CBV;
		break;
	case ComputeBindValueType::SRV:

		kind = ShaderBindingKind::SRV;
		break;
	case ComputeBindValueType::UAV:

		kind = ShaderBindingKind::UAV;
		break;
	case ComputeBindValueType::AccelStruct:

		kind = ShaderBindingKind::AccelStruct;
		break;
	}
	// 名前が指定されている場合は名前で検索し、そうでない場合はバインドポイントとスペースで検索
	if (!item.name.empty()) {
		return pipeline_->FindBindingByName(item.name, kind);
	}
	return pipeline_->FindBinding(kind, item.bindPoint, item.space);
}