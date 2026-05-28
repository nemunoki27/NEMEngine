#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Buffers/RenderBufferRegistry.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RegistryAutoBindTable class
	//	RenderBufferRegistry の全エントリとパイプラインスロットの対応をキャッシュするクラス。
	//	パイプライン変更時・レジストリエントリ数変化時のみ FindBindingByName を実行し、
	//	毎フレームは対象エントリへの registry.Find のみで GPU コマンドを直接発行する。
	//	ComputeおよびGraphicsの両バインドをサポートする。
	//============================================================================
	class RegistryAutoBindTable {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RegistryAutoBindTable() = default;
		~RegistryAutoBindTable() = default;

		// パイプラインまたはレジストリ構成が変わった時に再解決する（毎フレーム呼ぶ）
		// registry: スロット名の照合に使う（現フレームの登録が済んでいること）
		void Sync(const PipelineState& pipeline, const RenderBufferRegistry& registry);

		// Computeバインドを発行する（毎フレーム呼ぶ）
		void BindCompute(const RenderBufferRegistry& registry,
			ID3D12GraphicsCommandList* commandList) const;

		// Graphicsバインドを発行する（毎フレーム呼ぶ）
		void BindGraphics(const RenderBufferRegistry& registry,
			ID3D12GraphicsCommandList* commandList) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// パイプラインにスロットが存在するエントリのキャッシュ
		struct ResolvedEntry {

			// registry.Find に使うエイリアス名
			std::string alias;
			// 各バインド種別のロケーション（存在しない場合は nullptr）
			const RootBindingLocation* cbvLocation = nullptr;
			const RootBindingLocation* srvLocation = nullptr;
			const RootBindingLocation* uavLocation = nullptr;
			const RootBindingLocation* accelStructLocation = nullptr;
		};

		//--------- variables ----------------------------------------------------

		std::vector<ResolvedEntry> resolvedEntries_;
		const PipelineState* lastPipeline_ = nullptr;
		// レジストリエントリ数が変わった時も再解決する
		size_t lastRegistryCount_ = SIZE_MAX;
	};
} // Engine
