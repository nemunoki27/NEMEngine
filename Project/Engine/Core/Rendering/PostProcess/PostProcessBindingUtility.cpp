#include "PostProcessBindingUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingNames.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphBindingNames.h>

namespace Engine {

	namespace {
		// 予約済みのバインド名は共有定義を使う、これらと一致するバインディングは現在のパスのターゲットが割り当てられる
		constexpr const char* kSourceColorName = PostProcessBindingNames::kSourceColor;
		constexpr const char* kSourceDepthName = PostProcessBindingNames::kSourceDepth;
		constexpr const char* kDestColorName = PostProcessBindingNames::kDestColor;
	}

	bool IsResourceBinding(const ShaderResourceBinding& binding) {
		// SRV(テクスチャ入力)またはUAV(ランダムアクセス出力)であるかを確認
		return binding.kind == ShaderBindingKind::SRV ||
			binding.kind == ShaderBindingKind::UAV;
	}

	bool RequiresSourceDepth(const PipelineState& pipelineState) {
		// シェーダのリフレクション情報を走査し、深度テクスチャのバインディングが必要か判定
		for (const ShaderResourceBinding& binding : pipelineState.GetComputeReflection().resources) {
			if (binding.kind != ShaderBindingKind::SRV) {
				continue;
			}
			// 名前による一致確認
			if (binding.name == kSourceDepthName) {
				return true;
			}
			// 無名の場合でも標準レジスタ規約(t1)に合致すれば深度とみなす
			if (binding.name.empty() && binding.bindPoint == 1 && binding.space == 0) {
				return true;
			}
		}
		return false;
	}

	std::string MakePostProcessLogHeader(const MaterialAsset& material, const PostProcessExecutionDesc& desc) {
		// 実行中のポストプロセスを特定しやすくするためのログ用識別文字列を作成
		return "[PostProcess] material=" + material.name + " pass=" +
			std::string(EnumAdapter<MaterialPassKind>::ToStringView(desc.passKind)) + " ";
	}

	RenderTexture2D* GetFirstColor(MultiRenderTarget* target) {
		// MRTの中から最初のカラーテクスチャを抽出する単一出力パスでの簡易取得用
		if (!target || target->GetColorCount() == 0) {
			return nullptr;
		}
		return target->GetColorTexture(0);
	}

	MultiRenderTarget* ResolveExtraSource(const SceneExecutionContext& context, const std::string& targetName) {
		if (!context.targetRegistry || targetName.empty()) {
			return nullptr;
		}
		// "SceneMain"などのエイリアス名で直接検索
		if (MultiRenderTarget* byAlias = context.targetRegistry->Find(targetName)) {
			return byAlias;
		}

		// 見つからない場合は一時的なセット参照として解決を試みる
		RenderTargetSetReference reference{};
		reference.colors = { targetName };
		return context.targetRegistry->Resolve(reference);
	}

	DepthTexture2D* ResolveSourceDepth(
		const SceneExecutionContext& context,
		const PostProcessExecutionDesc& desc,
		MultiRenderTarget& source) {

		if (DepthTexture2D* depth = source.GetDepthTexture()) {
			return depth;
		}
		if (!context.targetRegistry) {
			return nullptr;
		}
		const auto found = desc.extraSources.find(kSourceDepthName);
		if (found == desc.extraSources.end()) {
			return nullptr;
		}
		if (DepthTexture2D* depth =
			context.targetRegistry->FindDepthByName(found->second)) {

			return depth;
		}
		MultiRenderTarget* target = ResolveExtraSource(
			context, found->second);
		return target ? target->GetDepthTexture() : nullptr;
	}

	bool AppendSRVBinding(const ShaderResourceBinding& binding,
		GraphicsCore& graphicsCore, const SceneExecutionContext& context,
		const PostProcessExecutionDesc& desc, MultiRenderTarget& source,
		std::vector<ComputeBindItem>& outBindItems, const std::string& logHeader) {

		RenderTexture2D* sourceColor = GetFirstColor(&source);
		DepthTexture2D* sourceDepth = ResolveSourceDepth(
			context, desc, source);
		RenderTexture2D* texture = nullptr;
		DepthTexture2D* depth = nullptr;
		std::string resolvedName{};

		// 名前が取れている場合は標準名を優先して解決し入力カラーまたは深度を割り当てる
		if (binding.name == kSourceColorName) {
			texture = sourceColor;
			resolvedName = kSourceColorName;
		}
		else if (binding.name == kSourceDepthName) {
			depth = sourceDepth;
			resolvedName = kSourceDepthName;
		}
		else if (!binding.name.empty()) {
			const auto resolveGraphSource = [&]() -> std::string_view {

				if (binding.name == ShaderGraphBindingNames::kSceneColor) return RenderTargetNames::kSceneColorOpaque;
				if (binding.name == ShaderGraphBindingNames::kSceneDepth) return RenderTargetNames::kSceneDepth;
				if (binding.name == ShaderGraphBindingNames::kSceneNormal) return RenderTargetNames::kSceneNormalMain;
				if (binding.name == ShaderGraphBindingNames::kScenePosition) return RenderTargetNames::kScenePositionMain;
				if (binding.name == ShaderGraphBindingNames::kSceneMaterial) return RenderTargetNames::kSceneMaterialMain;
				if (binding.name == ShaderGraphBindingNames::kSceneEmissive) return RenderTargetNames::kSceneEmissiveMain;
				if (binding.name == ShaderGraphBindingNames::kSceneFlags) return RenderTargetNames::kSceneFlagsMain;
				return {};
			};
			const std::string_view graphSource = resolveGraphSource();
			if (!graphSource.empty() && context.targetRegistry) {
				const std::string graphSourceName(graphSource);
				if (RenderTexture2D* color = context.targetRegistry->FindColorByName(graphSourceName)) {
					texture = color;
					resolvedName = binding.name;
				} else if (DepthTexture2D* foundDepth = context.targetRegistry->FindDepthByName(graphSourceName)) {
					depth = foundDepth;
					resolvedName = binding.name;
				}
			}
			// 標準名以外はdesc.extraSourcesでの追加のパス入力指定を確認
			auto found = desc.extraSources.find(binding.name);
			if (!texture && !depth && found != desc.extraSources.end() && context.targetRegistry) {
				// GBufferの色名→特定アタッチメント、深度名→深度、エイリアス→color0 の順で解決する
				if (RenderTexture2D* color = context.targetRegistry->FindColorByName(found->second)) {
					texture = color;
					resolvedName = binding.name;
				} else if (DepthTexture2D* foundDepth = context.targetRegistry->FindDepthByName(found->second)) {
					depth = foundDepth;
					resolvedName = binding.name;
				} else if (MultiRenderTarget* extra = ResolveExtraSource(context, found->second)) {
					texture = GetFirstColor(extra);
					resolvedName = binding.name;
				}
			}
		}

		// 古いシェーダや無名バインディング向けのレジストリ番号でのフォールバック
		if (!texture && !depth && binding.name.empty()) {
			if (binding.bindPoint == 0 && binding.space == 0) {
				texture = sourceColor;
				resolvedName = kSourceColorName;
			}
			else if (binding.bindPoint == 1 && binding.space == 0) {
				depth = sourceDepth;
				resolvedName = kSourceDepthName;
			}
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		// カラーテクスチャのバインドで適切なリソース状態へ遷移させる
		if (texture) {
			texture->Transition(*dxCommand,
				static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

			outBindItems.push_back({ binding.name.empty() ? std::string_view{} : std::string_view(binding.name),
				ComputeBindValueType::SRV, 0, texture->GetSRVGPUHandle(),
				binding.bindPoint, binding.space });
			return true;
		}
		// 深度テクスチャのバインド
		if (depth) {
			depth->Transition(*dxCommand,
				static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

			outBindItems.push_back({ binding.name.empty() ? std::string_view{} : std::string_view(binding.name),
				ComputeBindValueType::SRV, 0, depth->GetSRVGPUHandle(),
				binding.bindPoint, binding.space });
			return true;
		}

		// マテリアル側で指定されたノイズテクスチャ等の個別テクスチャオーバーライドを解決
		if (!binding.name.empty()) {
			auto found = desc.textureOverrides.find(binding.name);
			if (found != desc.textureOverrides.end() && found->second) {
				const GPUTextureResource* gpuTex =
					RuntimeTextureResolver::Resolve(graphicsCore, context.assetDatabase, found->second);
				if (gpuTex && gpuTex->valid) {
					outBindItems.push_back({ std::string_view(binding.name),
						ComputeBindValueType::SRV, 0, gpuTex->gpuHandle,
						binding.bindPoint, binding.space });
					return true;
				}
			}
		}

		// 予約済みの名前なのに解決できなかった場合はカラーや深度が必須なのに無いエラー
		const bool isSourceReserved =
			(binding.name == kSourceColorName || binding.name == kSourceDepthName) ||
			((binding.bindPoint == 0 || binding.bindPoint == 1) && binding.space == 0 && binding.name.empty());
		if (isSourceReserved) {
			Logger::Output(LogType::Engine, logHeader + "SRVのBindingを解決できません binding=" +
				(binding.name.empty() ? std::to_string(binding.bindPoint) : binding.name));
			return false;
		}

		// 任意のテクスチャバインディングが未設定の場合は、黒画面やエラーを防ぐため白テクスチャを割り当て
		const GPUTextureResource* white = graphicsCore.GetBuiltinTextureLibrary().GetWhiteTexture();
		if (!white || !white->valid) {
			Logger::Output(LogType::Engine, logHeader +
				"SRVのBindingを解決できず白Textureも利用できません binding=" +
				(binding.name.empty() ? std::to_string(binding.bindPoint) : binding.name));
			return false;
		}

		Logger::Output(LogType::Engine, logHeader + "Texture '" +
			(binding.name.empty() ? std::to_string(binding.bindPoint) : binding.name) +
			"'が未設定のためDefaultWhiteを使用します");
		outBindItems.push_back({ binding.name.empty() ? std::string_view{} : std::string_view(binding.name),
			ComputeBindValueType::SRV, 0, white->gpuHandle,
			binding.bindPoint, binding.space });
		return true;
	}

	bool AppendUAVBinding(const ShaderResourceBinding& binding,
		GraphicsCore& graphicsCore, const SceneExecutionContext& context,
		const PostProcessExecutionDesc& desc, MultiRenderTarget& dest,
		std::vector<ComputeBindItem>& outBindItems, const std::string& logHeader) {

		RenderTexture2D* destColor = nullptr;
		if (!binding.name.empty()) {
			const auto output = desc.outputTargets.find(binding.name);
			if (output != desc.outputTargets.end() && context.targetRegistry) {
				destColor = context.targetRegistry->FindColorByName(output->second);
				if (!destColor) {
					MultiRenderTarget* target = ResolveExtraSource(context, output->second);
					destColor = GetFirstColor(target);
				}
			}
		}

		// 標準出力は従来通りdestの先頭カラーへ割り当てる
		const bool isDestColor = binding.name == kDestColorName ||
			(binding.name.empty() && binding.bindPoint == 0 && binding.space == 0);
		if (!destColor && isDestColor) {
			destColor = GetFirstColor(&dest);
		}
		if (!destColor) {
			Logger::Output(LogType::Engine, logHeader + "出力先Colorがありません");
			return false;
		}
		// コンピュート書き込みにはUAVデスクリプタが必須
		if (destColor->GetUAVGPUHandle().ptr == 0) {
			Logger::Output(LogType::Engine, logHeader + "出力先ColorにUAV Descriptorがありません");
			return false;
		}

		// リソースをUAV状態に遷移させ、書き込み準備を整える
		destColor->Transition(*graphicsCore.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		outBindItems.push_back({ binding.name.empty() ? std::string_view{} : std::string_view(binding.name),
			ComputeBindValueType::UAV, 0, destColor->GetUAVGPUHandle(),
			binding.bindPoint, binding.space });
		return true;
	}

} // Engine
