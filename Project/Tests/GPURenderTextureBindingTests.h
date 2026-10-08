#pragma once

#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

namespace NEMTests {

	// Camera出力の読み取り条件と公開世代を確認する
	bool CheckRenderTextureBindings(ID3D12Device* device, Engine::GraphicsResourceRetirement& retirement);
}
