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
#include <unordered_set>

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
}

void Engine::PostProcessExecutor::Release() {

	constantBufferAllocator_.Release();
	parameterLayoutCache_.clear();
	elapsedTime_ = 0.0f;
	frameIndex_ = 0;
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

	// これから実行されるポスト識別名を取得
	const std::string logHeader = MakePostProcessLogHeader(*materialAsset, desc);

	// 書き込み先と書き込み元が同じなら処理しない
	if (sourceColor->GetResource() == destColor->GetResource()) {
		Logger::Output(LogType::Engine, logHeader +
			"入力と出力が同じResourceです Computeの同一Resource書き込みは許可されません");
		return false;
	}

	// 描画パスをマテリアルから取得
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, desc.passKind);
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Compute) {
		Logger::Output(LogType::Engine, logHeader + "Compute Passが見つかりません");
		return false;
	}

	// パイプラインを取得
	PipelineStaticSamplerOverrideSet samplerOverrides{};
	samplerOverrides.fillMissingSamplers = true;
	samplerOverrides.byName = desc.samplerOverrides;
	const PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
		assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {},
		DXGI_FORMAT_UNKNOWN,
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures(),
		nullptr, false, &samplerOverrides, passBinding->shaderOverride);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		Logger::Output(LogType::Engine, logHeader + "Pipelineがないかコンパイルに失敗しました");
		return false;
	}

	// 深度リソースをポストプロセスで使用するか
	const bool requiresDepth = RequiresSourceDepth(*pipelineState);
	// 使用するのに深度リソースがない場合
	if (requiresDepth && !ResolveSourceDepth(context, desc, *source)) {
		Logger::Output(LogType::Engine, logHeader +
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

	std::vector<ComputeBindItem> binds{};
	binds.reserve(8);
	// リフレクション情報からバッファをバインドする
	const ShaderReflectionInfo& reflection = pipelineState->GetComputeReflection();
	for (const ShaderResourceBinding& binding : reflection.resources) {
		if (!IsResourceBinding(binding)) {
			continue;
		}

		if (binding.kind == ShaderBindingKind::SRV) {
			if (!AppendSRVBinding(binding, graphicsCore, context, desc, *source, binds, logHeader)) {
				return false;
			}
			continue;
		}
		if (binding.kind == ShaderBindingKind::UAV) {
			if (!AppendUAVBinding(binding, graphicsCore, context,
				desc, *dest, binds, logHeader)) {

				return false;
			}
			continue;
		}
	}

	// パイプラインキャッシュを解決し、フレーム定数バインドの有無も初回のみ解決してキャッシュする
	auto layoutIt = parameterLayoutCache_.find(pipelineState);
	if (layoutIt == parameterLayoutCache_.end()) {

		PipelineCacheEntry entry{};
		entry.layout.Build(reflection, kParameterConstantsName);
		entry.hasFrameConstantsByName = (pipelineState->FindBindingByName(kFrameConstantsName, ShaderBindingKind::CBV) != nullptr);
		entry.hasFrameConstantsByRegister = (pipelineState->FindBinding(ShaderBindingKind::CBV, 0, 0) != nullptr);
		layoutIt = parameterLayoutCache_.emplace(pipelineState, std::move(entry)).first;
	}
	PipelineCacheEntry& cacheEntry = layoutIt->second;

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
			};
		};
		std::vector<uint8_t> bytes;
		if (!desc.parameterOverrides.empty()) {

			MaterialAsset merged = *materialAsset;
			merged.parameters.MergeFrom(desc.parameterOverrides);
			bytes = MaterialParameterBufferBuilder::Build(
				merged, parameterLayout, resolveTexture);
		} else {
			bytes = MaterialParameterBufferBuilder::Build(
				*materialAsset, parameterLayout, resolveTexture);
		}

		auto allocation = constantBufferAllocator_.AllocateAndUploadBytes(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), bytes);

		binds.push_back({ kParameterConstantsName, ComputeBindValueType::CBV,
			allocation.gpuAddress, {}, parameterLayout.GetBindPoint(), parameterLayout.GetSpace() });
	}

	lastExecutedMaterial_ = desc.material;
	lastExecutedLayout_ = parameterLayout.IsValid() ? &parameterLayout : nullptr;
	lastExecutedSRVBindings_.clear();
	lastExecutedSamplerBindings_.clear();
	for (const auto& binding : reflection.resources) {
		if (binding.kind == ShaderBindingKind::SRV &&
			binding.name != kSourceColorName &&
			binding.name != kSourceDepthName) {
			lastExecutedSRVBindings_.push_back(binding);
		}
		if (binding.kind == ShaderBindingKind::Sampler) {
			lastExecutedSamplerBindings_.push_back(binding);
		}
	}
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

	// 名前付き出力を含む全UAVの書き込み完了を後続Passへ公開する
	std::unordered_set<RenderTexture2D*> outputTextures{ destColor };
	for (const auto& [name, targetName] : desc.outputTargets) {

		if (RenderTexture2D* output =
			context.targetRegistry->FindColorByName(targetName)) {

			outputTextures.insert(output);
		}
	}
	for (RenderTexture2D* output : outputTextures) {

		dxCommand->UAVBarrier(output->GetResource());
		output->Transition(*dxCommand,
			static_cast<D3D12_RESOURCE_STATES>(
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}

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

	const ShaderReflectionInfo& reflection = pipelineState->GetComputeReflection();

	// PostProcessParameters CBufferの変数一覧を取得する
	auto layoutIt = parameterLayoutCache_.find(pipelineState);
	if (layoutIt == parameterLayoutCache_.end()) {
		PipelineCacheEntry entry{};
		entry.layout.Build(reflection, kParameterConstantsName);
		entry.hasFrameConstantsByName = (pipelineState->FindBindingByName(kFrameConstantsName, ShaderBindingKind::CBV) != nullptr);
		entry.hasFrameConstantsByRegister = (pipelineState->FindBinding(ShaderBindingKind::CBV, 0, 0) != nullptr);
		layoutIt = parameterLayoutCache_.emplace(pipelineState, std::move(entry)).first;
	}
	outVars = layoutIt->second.layout.GetVariables();

	// gSourceColor / gSourceDepthを除くユーザー向けSRVを収集する
	outSRVs.clear();
	outSamplers.clear();
	for (const auto& binding : reflection.resources) {
		if (binding.kind == ShaderBindingKind::SRV &&
			binding.name != kSourceColorName &&
			binding.name != kSourceDepthName) {
			outSRVs.push_back(binding);
		}
		if (binding.kind == ShaderBindingKind::Sampler) {
			outSamplers.push_back(binding);
		}
	}

	return true;
}
