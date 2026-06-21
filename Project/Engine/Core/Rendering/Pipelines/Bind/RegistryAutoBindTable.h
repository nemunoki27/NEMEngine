#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RegistryAutoBindTable class
	// RenderBufferRegistryの全エントリとパイプラインスロットの対応をキャッシュするクラス
	//	パイプライン変更時・レジストリエントリ数変化時のみFindBindingByNameを実行し、
	// 毎フレームは対象エントリへのregistry.FindのみでGPUコマンドを直接発行する
	// ComputeおよびGraphicsの両バインドをサポートする
	//============================================================================
	class RegistryAutoBindTable {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RegistryAutoBindTable() = default;
		~RegistryAutoBindTable() = default;

		// パイプラインまたはレジストリ構成が変わった時に再解決する毎フレーム呼び出し
		// registryはスロット名の照合に使い現フレームの登録が済んでいること
		void Sync(const PipelineState& pipeline, const RenderBufferRegistry& registry);

		// Computeバインドを発行する毎フレーム呼び出し
		void BindCompute(const RenderBufferRegistry& registry,
			ID3D12GraphicsCommandList* commandList) const;

		// Graphicsバインドを発行する毎フレーム呼び出し
		void BindGraphics(const RenderBufferRegistry& registry,
			ID3D12GraphicsCommandList* commandList) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// パイプラインにスロットが存在するエントリのキャッシュ
		struct ResolvedEntry {

			// 対象エントリのエイリアス名(再解決時の照合用)
			std::string alias;
			// registry.GetEntries内のインデックスで毎描画はFindせず直接参照する
			size_t entryIndex = 0;
			// 各バインド種別のロケーションで存在しない場合はnullptr
			const RootBindingLocation* cbvLocation = nullptr;
			const RootBindingLocation* srvLocation = nullptr;
			const RootBindingLocation* uavLocation = nullptr;
			const RootBindingLocation* accelStructLocation = nullptr;
		};

		//--------- variables ----------------------------------------------------

		std::vector<ResolvedEntry> resolvedEntries_;
		const PipelineState* lastPipeline_ = nullptr;
		// レジストリ実体が変わった時も再解決する(インデックス参照の整合のため)
		const RenderBufferRegistry* lastRegistry_ = nullptr;
		// レジストリエントリ数が変わった時も再解決する
		size_t lastRegistryCount_ = SIZE_MAX;
	};
} // Engine

