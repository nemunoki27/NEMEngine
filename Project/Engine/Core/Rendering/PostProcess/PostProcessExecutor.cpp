#include "PostProcessExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingUtility.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingNames.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>

//============================================================================
//	PostProcessExecutor classMethods
//============================================================================
namespace {

	constexpr const char* kFrameConstantsName = "PostProcessFrameConstants";
	constexpr const char* kParameterConstantsName = "PostProcessParameters";
	// 予約済みの入力名は共有定義を使う、出力名gDestColorはBindingUtility側でのみ参照する
	constexpr const char* kSourceColorName = Engine::PostProcessBindingNames::kSourceColor;
	constexpr const char* kSourceDepthName = Engine::PostProcessBindingNames::kSourceDepth;
}

void Engine::PostProcessExecutor::BeginFrame(float deltaTime) {

	elapsedTime_ += deltaTime;
	++frameIndex_;
	constantBufferAllocator_.BeginFrame();
	lastExecutedEntry_ = nullptr;
	lastExecutedLayout_ = nullptr;
	lastExecutedPipelineID_ = 0;
	// 未使用のShaderレイアウトを保持し続けない
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	std::erase_if(parameterLayoutCache_, [serial](const auto& entry) {
		return HasExpiredGraphicsResource(entry.second.lastUsedSerial, serial);
	});
}

void Engine::PostProcessExecutor::Release() {

	constantBufferAllocator_.Release();
	ClearParameterLayoutCache();
	outputTextureScratch_.clear();
	bindScratch_.clear();
	parameterScratch_.clear();
	typedLoadSupport_.clear();
	elapsedTime_ = 0.0f;
	frameIndex_ = 0;
}

void Engine::PostProcessExecutor::ClearParameterLayoutCache() {

	// 借用を解除してから所有する型情報を破棄する
	lastExecutedEntry_ = nullptr;
	lastExecutedLayout_ = nullptr;
	lastExecutedPipelineID_ = 0;
	parameterLayoutCache_.clear();
}

const std::vector<Engine::ShaderResourceBinding>& Engine::PostProcessExecutor::GetLastExecutedSRVBindings() const {

	return lastExecutedEntry_ ? lastExecutedEntry_->resources : emptyBindings_;
}

const std::vector<Engine::ShaderResourceBinding>& Engine::PostProcessExecutor::GetLastExecutedSamplerBindings() const {

	return lastExecutedEntry_ ? lastExecutedEntry_->samplers : emptyBindings_;
}

bool Engine::PostProcessExecutor::Execute(GraphicsCore& graphicsCore, [[maybe_unused]] const RenderFrameRequest& request,
	const SceneExecutionContext& context, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, const PostProcessExecutionDesc& desc) {

	if (!context.targetRegistry || !context.assetDatabase) {
		return false;
	}

	// レンダーターゲットを取得
	MultiRenderTarget* source = context.targetRegistry->Resolve(desc.source);
	MultiRenderTarget* dest = context.targetRegistry->Resolve(desc.dest);
	RenderTexture2D* sourceColor = GetFirstColor(source);
	RenderTexture2D* destColor = GetFirstColor(dest);
	if (!source || !dest || !sourceColor || !destColor) {
		return false;
	}

	//============================================================================
	//	ポストプロセスの実行に必要なアセットの取得
	//============================================================================
	// ポストプロセスマテリアル取得
	const MaterialAsset* materialAsset = assetLibrary.LoadMaterial(desc.material);
	if (!materialAsset) {
		Logger::Output(LogType::Engine, "[PostProcess] Materialが見つかりません");
		return false;
	}

	// 書き込み先と書き込み元が同じなら処理しない
	if (sourceColor->GetResource() == destColor->GetResource() && !desc.allowInPlace) {
		Logger::Output(LogType::Engine, MakePostProcessLogHeader(*materialAsset, desc) +
			"入力と出力が同じResourceです Computeの同一Resource書き込みは許可されません");
		return false;
	}

	// 描画パスをマテリアルから取得
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, desc.passKind);
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Compute) {
		Logger::Output(LogType::Engine, MakePostProcessLogHeader(*materialAsset, desc) + "Compute Passが見つかりません");
		return false;
	}

	// パイプラインを取得
	PipelineStaticSamplerOverrideSet samplerOverrides{};
	samplerOverrides.fillMissingSamplers = true;
	samplerOverrides.byName = desc.samplerOverrides;
	// CameraのAOだけは影の希望設定から独立して実行する
	auto runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	if (desc.allowInlineRayTracing) {
		runtimeFeatures.useInlineRayTracing = graphicsCore.GetDXObject().GetFeatureController().GetSupport().SupportsInlineRayTracingPath();
	}
	const PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
		assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {},
		DXGI_FORMAT_UNKNOWN,
		runtimeFeatures,
		nullptr, false, &samplerOverrides, passBinding->shaderOverride);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		Logger::Output(LogType::Engine, MakePostProcessLogHeader(*materialAsset, desc) + "Pipelineがないかコンパイルに失敗しました");
		return false;
	}

	// 深度リソースをポストプロセスで使用するか
	PipelineCacheEntry& cacheEntry = GetPipelineEntry(*pipelineState);
	if (desc.allowInPlace) {
		// SRVとの別名参照はBindingUtilityで拒否し、出力は一枚だけに限定する
		if (!desc.outputTargets.empty() || !destColor->GetUAVGPUHandle().ptr) return false;
		uint32_t outputCount = 0;
		for (const auto& binding : pipelineState->GetComputeReflection().resources) {
			if (binding.bindCount != 1 || binding.kind == ShaderBindingKind::AccelStruct ||
				binding.name == "gNEMGlobalTexture2D" || binding.name == "gNEMGlobalTextureCube") return false;
		}
		for (const auto& binding : cacheEntry.bindings) {
			if (binding.kind != ShaderBindingKind::UAV) continue;
			if (binding.name != PostProcessBindingNames::kDestColor || binding.bindPoint != 0 || binding.space != 0) return false;
			++outputCount;
		}
		if (outputCount != 1) return false;
		const auto format = destColor->GetFormat();
		auto [support, inserted] = typedLoadSupport_.try_emplace(format, false);
		if (inserted) {
			D3D12_FEATURE_DATA_FORMAT_SUPPORT query{format};
			support->second = SUCCEEDED(graphicsCore.GetDXObject().GetDevice()->CheckFeatureSupport(
				D3D12_FEATURE_FORMAT_SUPPORT, &query, sizeof(query))) &&
				(query.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0 &&
				(query.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
		}
		if (!support->second) return false;
	}
	// 使用するのに深度リソースがない場合
	if (cacheEntry.requiresDepth && !ResolveSourceDepth(context, desc, *source)) {
		Logger::Output(LogType::Engine, MakePostProcessLogHeader(*materialAsset, desc) +
			"gSourceDepthが必要ですが入力Depthがありません");
		return false;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	//============================================================================
	//	パイプライン・描画バインディング
	//============================================================================
	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());
	pipelineState->BindGlobalDescriptorTablesCompute(commandList,
		graphicsCore.GetSRVDescriptor().GetGPUHandle(0));

	auto& binds = bindScratch_;
	binds.clear();
	binds.reserve(cacheEntry.bindings.size() + 2);
	// リフレクション情報からバッファをバインドする
	for (const ShaderResourceBinding& binding : cacheEntry.bindings) {
		if (binding.kind == ShaderBindingKind::SRV || binding.kind == ShaderBindingKind::AccelStruct) {
			if (!AppendSRVBinding(binding, graphicsCore, context, desc, *source, binds, *materialAsset)) {
				return false;
			}
			continue;
		}
		if (binding.kind == ShaderBindingKind::UAV) {
			if (!AppendUAVBinding(binding, graphicsCore, context,
				desc, *dest, binds, *materialAsset)) {

				return false;
			}
			continue;
		}
		if (binding.kind == ShaderBindingKind::CBV) {
			// ライト等の共通定数を同じViewの登録内容から取得する
			const auto* buffer = context.bufferRegistry.Find(binding.name);
			if (!buffer || !buffer->gpuAddress) {
				Logger::Output(LogType::Engine, MakePostProcessLogHeader(*materialAsset, desc) + "定数Bufferを解決できません binding=" + binding.name);
				return false;
			}
			binds.push_back({binding.name, ComputeBindValueType::CBV, buffer->gpuAddress, {}, binding.bindPoint, binding.space});
		}
	}

	// 共通フレーム、ポストプロセス実行情報のバッファデータを構築してバインド
	if (cacheEntry.hasFrameConstantsByName || cacheEntry.hasFrameConstantsByRegister) {

		PostProcessFrameConstants constants{};
		constants.resolution = Vector2(static_cast<float>(dest->GetWidth()), static_cast<float>(dest->GetHeight()));
		constants.invResolution = Vector2(1.0f / (std::max)(constants.resolution.x, 1.0f), 1.0f / (std::max)(constants.resolution.y, 1.0f));
		constants.time = elapsedTime_;
		constants.deltaTime = context.systemContext->unscaledDeltaTime;
		constants.frameIndex = frameIndex_;
		// カメラ情報を設定
		constants.cameraNear = 0.1f;
		constants.cameraFar = 1000.0f;
		if (context.view) {
			if (const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective); camera && camera->valid) {

				constants.cameraNear = camera->nearClip;
				constants.cameraFar = camera->farClip;
				constants.cameraWorldPos = camera->cameraPos;
				constants.cameraView = camera->matrices.viewMatrix;
				constants.cameraViewInverse = camera->matrices.inverseViewMatrix;
				constants.cameraProjection = camera->matrices.projectionMatrix;
				constants.cameraProjectionInverse = camera->matrices.inverseProjectionMatrix;
			}
		}

		auto allocation = constantBufferAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), constants);
		binds.push_back({ cacheEntry.hasFrameConstantsByName ? std::string_view(kFrameConstantsName) : std::string_view{},
			ComputeBindValueType::CBV, allocation.gpuAddress, {}, 0, 0 });
	}

	// ポストプロセス固有のパラメータバッファの構築
	// バッファがあればバインド
	MaterialParameterLayout& parameterLayout = cacheEntry.layout;
	if (parameterLayout.IsValid()) {

		const auto resolveTexture = [&](MaterialParameterSemantic semantic,
			const AssetID& textureAssetID) {

			if (!textureAssetID) {
				return MaterialParameterBufferBuilder::TextureResolveResult{};
			}
			const RuntimeTextureResolver::BindlessResolveResult result =
				RuntimeTextureResolver::ResolveBindless(
					graphicsCore, context.assetDatabase, textureAssetID,
					IsSRGBMaterialTexture(semantic) ?
					TextureColorSpace::SRGB : TextureColorSpace::Linear);
			return MaterialParameterBufferBuilder::TextureResolveResult{
				.index = result.srvIndex,
				.cacheable = !result.retry,
				.ownership = result.ownership,
			};
		};
		MaterialParameterSet merged;
		const MaterialParameterSet* parameters = &materialAsset->parameters;
		if (!desc.parameterOverrides.empty()) {
			merged = materialAsset->parameters;
			merged.MergeFrom(desc.parameterOverrides);
			parameters = &merged;
		}
		parameterScratch_.resize((std::max)(parameterLayout.GetSizeInBytes(), 16u));
		MaterialParameterBufferBuilder::BuildElementInto(parameterScratch_, *parameters,
			MaterialParameterSet{}, parameterLayout, resolveTexture);

		auto allocation = constantBufferAllocator_.AllocateAndUploadBytes(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), parameterScratch_);

		binds.push_back({ kParameterConstantsName, ComputeBindValueType::CBV,
			allocation.gpuAddress, {}, parameterLayout.GetBindPoint(), parameterLayout.GetSpace() });
	}

	lastExecutedMaterial_ = desc.material;
	lastExecutedLayout_ = parameterLayout.IsValid() ? &parameterLayout : nullptr;
	lastExecutedEntry_ = &cacheEntry;
	lastExecutedPipelineID_ = pipelineState->GetUniqueID();
	// 追加されたバインドデータをバインディング
	ComputeRootBinder binder{ *pipelineState };
	binder.Bind(commandList, binds);

	// ポストプロセス実行
	UINT dispatchX = 1;
	UINT dispatchY = 1;
	UINT dispatchZ = 1;
	if (desc.dispatchMode == ComputeDispatchMode::Fixed) {

		dispatchX = (std::max)(desc.groupCountX, 1u);
		dispatchY = (std::max)(desc.groupCountY, 1u);
		dispatchZ = (std::max)(desc.groupCountZ, 1u);
	} else {

		const uint32_t width = (desc.dispatchMode == ComputeDispatchMode::FromSourceSize) ? source->GetWidth() : dest->GetWidth();
		const uint32_t height = (desc.dispatchMode == ComputeDispatchMode::FromSourceSize) ? source->GetHeight() : dest->GetHeight();
		dispatchX = DxUtils::RoundUp(width, pipelineState->GetThreadGroupX());
		dispatchY = DxUtils::RoundUp(height, pipelineState->GetThreadGroupY());
		dispatchZ = 1;
	}
	commandList->Dispatch(dispatchX, dispatchY, dispatchZ);

	// 名前付き出力も同じ同期地点で読み取り状態へ遷移する
	outputTextureScratch_.clear();
	outputTextureScratch_.reserve(desc.outputTargets.size() + 1);
	outputTextureScratch_.push_back(destColor);
	for (const auto& [name, targetName] : desc.outputTargets) {

		if (RenderTexture2D* output =
			context.targetRegistry->FindColorByName(targetName)) {

			if (std::find(outputTextureScratch_.begin(), outputTextureScratch_.end(), output) ==
				outputTextureScratch_.end()) {
				outputTextureScratch_.push_back(output);
			}
		}
	}
	RenderTexture2D::TransitionAll(*dxCommand, outputTextureScratch_, static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

	return true;
}

bool Engine::PostProcessExecutor::TryGetReflection(GraphicsCore& graphicsCore,
	RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache,
	AssetID materialID, MaterialPassKind passKind,
	std::vector<ShaderConstantBufferVariable>& outVars,
	std::vector<ShaderResourceBinding>& outSRVs,
	std::vector<ShaderResourceBinding>& outSamplers) {

	const MaterialAsset* materialAsset = assetLibrary.LoadMaterial(materialID);
	if (!materialAsset) {
		return false;
	}

	const MaterialPassBinding* passBinding = FindPass(*materialAsset, passKind);
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Compute) {
		return false;
	}

	PipelineStaticSamplerOverrideSet samplerOverrides{};
	samplerOverrides.fillMissingSamplers = true;
	const PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
		assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {},
		DXGI_FORMAT_UNKNOWN,
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures(),
		nullptr, false, &samplerOverrides, passBinding->shaderOverride);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		return false;
	}

	// 描画と同じ型情報から編集用の値を取得する
	const auto& entry = GetPipelineEntry(*pipelineState);
	outVars = entry.layout.GetVariables();
	outSRVs = entry.resources;
	outSamplers = entry.samplers;

	return true;
}

Engine::PostProcessExecutor::PipelineCacheEntry& Engine::PostProcessExecutor::GetPipelineEntry(const PipelineState& pipeline) {

	auto layoutIt = parameterLayoutCache_.find(pipeline.GetUniqueID());
	if (layoutIt == parameterLayoutCache_.end()) {

		PipelineCacheEntry entry{};
		entry.layout.Build(pipeline.GetComputeReflection(), kParameterConstantsName);
		entry.hasFrameConstantsByName = (pipeline.FindBindingByName(kFrameConstantsName, ShaderBindingKind::CBV) != nullptr);
		entry.hasFrameConstantsByRegister = (pipeline.FindBinding(ShaderBindingKind::CBV, 0, 0) != nullptr);
		entry.requiresDepth = RequiresSourceDepth(pipeline);
		// Shaderの公開世代ごとに編集用の型情報も揃える
		for (const auto& binding : pipeline.GetComputeReflection().resources) {

			const bool resourceBinding = IsResourceBinding(binding);
			const bool sharedConstants = binding.kind == ShaderBindingKind::CBV && binding.name != kFrameConstantsName &&
				binding.name != kParameterConstantsName && !(binding.space == 0 && binding.bindPoint <= 1);
			if ((resourceBinding || sharedConstants) && binding.name != "gNEMGlobalTexture2D" && binding.name != "gNEMGlobalTextureCube") {
				entry.bindings.emplace_back(binding);
			}
			if (binding.kind == ShaderBindingKind::SRV && binding.name != kSourceColorName && binding.name != kSourceDepthName) {
				entry.resources.emplace_back(binding);
			} else if (binding.kind == ShaderBindingKind::Sampler) {
				entry.samplers.emplace_back(binding);
			}
		}
		layoutIt = parameterLayoutCache_.emplace(pipeline.GetUniqueID(), std::move(entry)).first;
	}
	PipelineCacheEntry& cacheEntry = layoutIt->second;
	cacheEntry.lastUsedSerial = GraphicsFrameState::GetFrameSerial();

	return cacheEntry;
}
