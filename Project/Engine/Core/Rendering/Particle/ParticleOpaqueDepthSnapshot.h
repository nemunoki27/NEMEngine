#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTexture2D.h>

// c++
#include <array>
#include <memory>

namespace Engine {

	class GraphicsCore;

	// 書き込み中の深度と分離したView単位の背景深度
	class ParticleOpaqueDepthSnapshot {
	public:
		// 現在のframeへ必要なときだけ深度をコピーする
		DepthTexture2D* Copy(GraphicsCore& graphics, DepthTexture2D& source);
		// 描画側と検証側で同じ深度コピーを共有する
		DepthTexture2D* Copy(DxCommand& commands, DSVDescriptor& depths, SRVDescriptor& shaders, DepthTexture2D& source);
		// frameごとの所有を入れ替える
		void Swap(ParticleOpaqueDepthSnapshot& other) noexcept;
		// 全frameの深度を回収へ渡す
		void Destroy();

		//--------- accessor -----------------------------------------------------

		DepthTexture2D* GetTexture() const;
		uint64_t GetFrameSerial() const;
		// Camera深度の再利用では最後に完成したframeを取得する
		DepthTexture2D* GetLatestTexture() const;
	private:
		struct Frame {

			std::unique_ptr<DepthTexture2D> texture;
			ID3D12Resource* source = nullptr;
			uint64_t serial = UINT64_MAX;
		};
		std::array<Frame, kGraphicsFrameContextCount> frames_;
		GraphicsFrameResourceRegistration frameRegistration_;

		// GPU完了後に無効なframe領域を回収する
		void ReleaseInactiveFrames(uint32_t count);
	};
}
