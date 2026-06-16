#include "MultiRenderTargetCopyUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

//============================================================================
//	MultiRenderTargetCopyUtility functions
//============================================================================
bool Engine::MultiRenderTargetCopy::CopyColor0Resource(GraphicsCore& graphicsCore,
	MultiRenderTarget* source, MultiRenderTarget* dest) {

	if (!source || !dest) {
		return false;
	}
	// blit失敗時のfallbackで、formatとサイズが完全一致するときだけresource copyできる
	RenderTexture2D* sourceColor = source->GetColorTexture(0);
	RenderTexture2D* destColor = dest->GetColorTexture(0);
	if (!sourceColor || !destColor ||
		sourceColor->GetFormat() != destColor->GetFormat() ||
		sourceColor->GetRenderTarget().width != destColor->GetRenderTarget().width ||
		sourceColor->GetRenderTarget().height != destColor->GetRenderTarget().height) {
		return false;
	}

	// copy元と先をそれぞれの状態へ遷移してからCopyResourceで丸ごと転送する
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);
	destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_DEST);
	dxCommand->GetCommandList()->CopyResource(destColor->GetResource(), sourceColor->GetResource());
	return true;
}
