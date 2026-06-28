#include "RegistryAutoBindTable.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

//============================================================================
//	RegistryAutoBindTable classMethods
//============================================================================

void RegistryAutoBindTable::Sync(const PipelineState& pipeline,
	const RenderBufferRegistry& registry) {

	// パイプライン・レジストリ実体・エントリ数が変わっていなければキャッシュを再利用
	if (pipeline.GetUniqueID() == lastPipelineID_ && &registry == lastRegistry_ &&
		registry.GetCount() == lastRegistryCount_) {
		return;
	}
	lastPipelineID_ = pipeline.GetUniqueID();
	lastRegistry_ = &registry;
	lastRegistryCount_ = registry.GetCount();
	resolvedEntries_.clear();

	// 全エントリをパイプラインスロットと照合し、一致するものだけインデックス付きでキャッシュする
	const std::vector<RegisteredRenderBuffer>& entries = registry.GetEntries();
	for (size_t i = 0; i < entries.size(); ++i) {

		const RegisteredRenderBuffer& entry = entries[i];

		const RootBindingLocation* cbv =
			pipeline.FindBindingByName(entry.alias, ShaderBindingKind::CBV);
		const RootBindingLocation* srv =
			pipeline.FindBindingByName(entry.alias, ShaderBindingKind::SRV);
		const RootBindingLocation* uav =
			pipeline.FindBindingByName(entry.alias, ShaderBindingKind::UAV);
		const RootBindingLocation* accel =
			pipeline.FindBindingByName(entry.alias, ShaderBindingKind::AccelStruct);

		if (!cbv && !srv && !uav && !accel) {
			continue;
		}
		resolvedEntries_.push_back({ entry.alias, i, cbv, srv, uav, accel });
	}
}

void RegistryAutoBindTable::BindCompute(const RenderBufferRegistry& registry,
	ID3D12GraphicsCommandList* commandList) const {

	const std::vector<RegisteredRenderBuffer>& entries = registry.GetEntries();
	for (const ResolvedEntry& resolved : resolvedEntries_) {

		// Syncで解決済みのインデックスで直接参照する(毎描画の文字列Findを避ける)
		if (resolved.entryIndex >= entries.size()) {
			continue;
		}
		const RegisteredRenderBuffer& entry = entries[resolved.entryIndex];
		if (resolved.cbvLocation && entry.gpuAddress != 0) {
			RootBindingCommand::SetComputeCBV(commandList, resolved.cbvLocation, entry.gpuAddress);
		}
		if (resolved.srvLocation && (entry.gpuAddress != 0 || entry.srvGPUHandle.ptr != 0)) {
			RootBindingCommand::SetComputeSRV(commandList, resolved.srvLocation,
				entry.gpuAddress, entry.srvGPUHandle);
		}
		if (resolved.uavLocation && (entry.gpuAddress != 0 || entry.uavGPUHandle.ptr != 0)) {
			RootBindingCommand::SetComputeUAV(commandList, resolved.uavLocation,
				entry.gpuAddress, entry.uavGPUHandle);
		}
	}
}

void RegistryAutoBindTable::BindGraphics(const RenderBufferRegistry& registry,
	ID3D12GraphicsCommandList* commandList) const {

	const std::vector<RegisteredRenderBuffer>& entries = registry.GetEntries();
	for (const ResolvedEntry& resolved : resolvedEntries_) {

		// Syncで解決済みのインデックスで直接参照する(毎描画の文字列Findを避ける)
		if (resolved.entryIndex >= entries.size()) {
			continue;
		}
		const RegisteredRenderBuffer& entry = entries[resolved.entryIndex];
		if (resolved.cbvLocation && entry.gpuAddress != 0) {
			RootBindingCommand::SetGraphicsCBV(commandList, resolved.cbvLocation, entry.gpuAddress);
		}
		if (resolved.srvLocation && (entry.gpuAddress != 0 || entry.srvGPUHandle.ptr != 0)) {
			RootBindingCommand::SetGraphicsSRV(commandList, resolved.srvLocation,
				entry.gpuAddress, entry.srvGPUHandle);
		}
		if (resolved.accelStructLocation && entry.gpuAddress != 0) {
			// AccelStructはSRVコマンドでgpuAddress直指定
			RootBindingCommand::SetGraphicsSRV(commandList, resolved.accelStructLocation,
				entry.gpuAddress, {});
		}
	}
}
