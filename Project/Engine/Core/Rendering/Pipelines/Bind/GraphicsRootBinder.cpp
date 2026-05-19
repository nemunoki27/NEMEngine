#include "GraphicsRootBinder.h"

//============================================================================
//	GraphicsRootBinder classMethods
//============================================================================

void Engine::GraphicsRootBinder::Bind(ID3D12GraphicsCommandList* commandList,
	const std::span<const GraphicsBindItem>& items) const {

	for (const GraphicsBindItem& item : items) {

		// バインド情報を取得
		const RootBindingLocation* binding = ResolveBinding(item);
		if (!binding) {
			continue;
		}
		// バインドの種類に応じてコマンドリストにバインド
		switch (item.type) {
		case GraphicsBindValueType::CBV:

			RootBindingCommand::SetGraphicsCBV(commandList, binding, item.gpuAddress);
			break;
		case GraphicsBindValueType::SRV:

			RootBindingCommand::SetGraphicsSRV(commandList, binding, item.gpuAddress, item.descriptor);
			break;
		case GraphicsBindValueType::AccelStruct:

			RootBindingCommand::SetGraphicsSRV(commandList, binding, item.gpuAddress, {});
			break;
		}
	}
}

const Engine::RootBindingLocation* Engine::GraphicsRootBinder::ResolveBinding(const GraphicsBindItem& item) const {

	// バインドの種類を取得
	ShaderBindingKind kind{};
	switch (item.type) {
	case GraphicsBindValueType::CBV:

		kind = ShaderBindingKind::CBV;
		break;
	case GraphicsBindValueType::SRV:

		kind = ShaderBindingKind::SRV;
		break;
	case GraphicsBindValueType::AccelStruct:

		kind = ShaderBindingKind::AccelStruct;
		break;
	}
	// 名前が指定されている場合は名前で検索し、そうでない場合はバインドポイントとスペースで検索
	if (!item.name.empty()) {

		return pipeline_->FindBindingByName(item.name, kind);
	}
	return pipeline_->FindBinding(kind, item.bindPoint, item.space);
}