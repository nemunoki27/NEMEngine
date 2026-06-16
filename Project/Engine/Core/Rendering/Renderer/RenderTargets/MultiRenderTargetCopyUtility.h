#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

namespace Engine {

	class GraphicsCore;

	//============================================================================
	//	MultiRenderTargetCopyUtility namespace
	//	MRT間のリソースコピー補助
	//============================================================================
	namespace MultiRenderTargetCopy {

		// sourceとdestのcolor0をCopyResourceで丸ごと転送する、formatとサイズが完全一致するときのみ成功する
		// blit失敗時のfallback用途で、転送後にsourceはCOPY_SOURCEのまま残す
		bool CopyColor0Resource(GraphicsCore& graphicsCore,
			MultiRenderTarget* source, MultiRenderTarget* dest);
	} // MultiRenderTargetCopy
} // Engine
