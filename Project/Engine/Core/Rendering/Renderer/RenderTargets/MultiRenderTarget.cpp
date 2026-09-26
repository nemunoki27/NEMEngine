#include "MultiRenderTarget.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxDepthStencilView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>

// c++
#include <stdexcept>

//============================================================================
//	MultiRenderTarget classMethods
//============================================================================
Engine::MultiRenderTarget::~MultiRenderTarget() {

	Destroy();
}

void Engine::MultiRenderTarget::Create(ID3D12Device* device,
	RTVDescriptor* rtvDescriptor, DSVDescriptor* dsvDescriptor,
	SRVDescriptor* srvDescriptor, const MultiRenderTargetCreateDesc& desc) {

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "MultiRenderTargetの作成を開始します");

	// 完成するまでは現在の描画先を維持する
	if (desc.width == 0 || desc.height == 0 || desc.colors.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) {
		throw std::invalid_argument("MultiRenderTargetのサイズまたは色数が不正です");
	}
	std::vector<std::unique_ptr<RenderTexture2D>> colors;
	std::unique_ptr<DepthTexture2D> depth;

	// サイズ設定
	uint32_t colorCount = static_cast<uint32_t>(desc.colors.size());

	// 色レンダーテクスチャの作成
	colors.reserve(colorCount);
	for (size_t i = 0; i < colorCount; ++i) {

		Logger::Output(LogType::Engine, "RenderTexture2D番号: {}", i);

		// 色レンダーテクスチャの情報を取得する
		const auto& color = desc.colors[i];

		// テクスチャ作成
		auto texture = std::make_unique<RenderTexture2D>();
		RenderTextureCreateDesc createDesc{};
		createDesc.width = desc.width;
		createDesc.height = desc.height;
		createDesc.format = color.format;
		createDesc.clearColor = color.clearColor;
		createDesc.createUAV = color.createUAV;
		createDesc.debugName = std::wstring(color.name.begin(), color.name.end());
		texture->Create(device, rtvDescriptor, srvDescriptor, createDesc);

		colors.emplace_back(std::move(texture));
	}
	// 深度レンダーテクスチャの作成
	if (desc.depth.has_value()) {

		depth = std::make_unique<DepthTexture2D>();
		depth->Create(dsvDescriptor, srvDescriptor, *desc.depth);
	}

	colors_.swap(colors);
	depth_.swap(depth);
	width_ = desc.width;
	height_ = desc.height;

	Logger::Output(LogType::Engine, "MultiRenderTargetを作成しました");
	Logger::EndSection(LogType::Engine);
}

void Engine::MultiRenderTarget::Destroy() {

	for (auto& color : colors_) {

		if (color) {
			color->Destroy();
			// RTV/SRVを持つRenderTextureはclear任せにせず、終了時に明示resetする
			color.reset();
		}
	}
	colors_.clear();

	if (depth_) {

		depth_->Destroy();
		depth_.reset();
	}
	width_ = 0;
	height_ = 0;
}

void Engine::MultiRenderTarget::TransitionForRender(DxCommand& dxCommand) {

	// レンダーターゲットへ遷移
	for (auto& color : colors_) {

		color->Transition(dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}
	if (depth_) {

		depth_->Transition(dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}
}

void Engine::MultiRenderTarget::TransitionForShaderRead(DxCommand& dxCommand) {

	for (auto& color : colors_) {

		color->Transition(dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	if (depth_) {

		depth_->Transition(dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}

void Engine::MultiRenderTarget::Bind(DxCommand& dxCommand) const {

	std::vector<RenderTarget> renderTargets = BuildRenderTargets();
	if (depth_) {

		dxCommand.BindRenderTargets(renderTargets, depth_->GetDSVCPUHandle());
	} else {

		dxCommand.BindRenderTargets(renderTargets, std::nullopt);
	}
	dxCommand.SetViewportAndScissor(width_, height_);
}

void Engine::MultiRenderTarget::Clear(DxCommand& dxCommand, const MultiRenderTargetClearDesc& desc) const {

	auto* commandList = dxCommand.GetCommandList();
	if (desc.clearColor) {
		for (const auto& color : colors_) {

			const RenderTarget& renderTarget = color->GetRenderTarget();
			// クリアカラーを設定
			float clearColor[4] = { renderTarget.clearColor.r,renderTarget.clearColor.g,
				renderTarget.clearColor.b,renderTarget.clearColor.a };
			if (desc.clearColorValue.has_value()) {

				clearColor[0] = desc.clearColorValue->r;
				clearColor[1] = desc.clearColorValue->g;
				clearColor[2] = desc.clearColorValue->b;
				clearColor[3] = desc.clearColorValue->a;
			}
			// レンダーターゲットをクリア
			commandList->ClearRenderTargetView(renderTarget.rtvHandle, clearColor, 0, nullptr);
		}
	}
	if (depth_ && (desc.clearDepth || desc.clearStencil)) {

		D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
		if (desc.clearDepth) {
			flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_DEPTH);
		}
		if (desc.clearStencil) {
			flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_STENCIL);
		}
		// 深度クリア
		commandList->ClearDepthStencilView(depth_->GetDSVCPUHandle(), flags,
			desc.clearDepthValue, desc.clearStencilValue, 0, nullptr);
	}
}

std::vector<Engine::RenderTarget> Engine::MultiRenderTarget::BuildRenderTargets() const {

	std::vector<RenderTarget> result{};
	result.reserve(colors_.size());
	for (const auto& color : colors_) {

		result.emplace_back(color->GetRenderTarget());
	}
	return result;
}
