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

	// パイプラインとレジストリ構成が変わっていなければキャッシュを再利用
	if (&pipeline == lastPipeline_ && registry.GetCount() == lastRegistryCount_) {
		return;
	}
	lastPipeline_ = &pipeline;
	lastRegistryCount_ = registry.GetCount();
	resolvedEntries_.clear();

	// 全エントリをパイプラインスロットと照合し、一致するものだけキャッシュする
	for (const RegisteredRenderBuffer& entry : registry.GetEntries()) {

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
		resolvedEntries_.push_back({ entry.alias, cbv, srv, uav, accel });
	}
}

void RegistryAutoBindTable::BindCompute(const RenderBufferRegistry& registry,
	ID3D12GraphicsCommandList* commandList) const {

	for (const ResolvedEntry& resolved : resolvedEntries_) {

		const RegisteredRenderBuffer* entry = registry.Find(resolved.alias);
		if (!entry) {
			continue;
		}
		if (resolved.cbvLocation && entry->gpuAddress != 0) {
			RootBindingCommand::SetComputeCBV(commandList, resolved.cbvLocation, entry->gpuAddress);
		}
		if (resolved.srvLocation && (entry->gpuAddress != 0 || entry->srvGPUHandle.ptr != 0)) {
			RootBindingCommand::SetComputeSRV(commandList, resolved.srvLocation,
				entry->gpuAddress, entry->srvGPUHandle);
		}
		if (resolved.uavLocation && (entry->gpuAddress != 0 || entry->uavGPUHandle.ptr != 0)) {
			RootBindingCommand::SetComputeUAV(commandList, resolved.uavLocation,
				entry->gpuAddress, entry->uavGPUHandle);
		}
	}
}

void RegistryAutoBindTable::BindGraphics(const RenderBufferRegistry& registry,
	ID3D12GraphicsCommandList* commandList) const {

	for (const ResolvedEntry& resolved : resolvedEntries_) {

		const RegisteredRenderBuffer* entry = registry.Find(resolved.alias);
		if (!entry) {
			continue;
		}
		if (resolved.cbvLocation && entry->gpuAddress != 0) {
			RootBindingCommand::SetGraphicsCBV(commandList, resolved.cbvLocation, entry->gpuAddress);
		}
		if (resolved.srvLocation && (entry->gpuAddress != 0 || entry->srvGPUHandle.ptr != 0)) {
			RootBindingCommand::SetGraphicsSRV(commandList, resolved.srvLocation,
				entry->gpuAddress, entry->srvGPUHandle);
		}
		if (resolved.accelStructLocation && entry->gpuAddress != 0) {
			// AccelStructはSRVコマンドでgpuAddress直指定
			RootBindingCommand::SetGraphicsSRV(commandList, resolved.accelStructLocation,
				entry->gpuAddress, {});
		}
	}
}
