#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

namespace Engine {

	class GraphicsCore;

	namespace RuntimeTextureResolver {

		// アセットIDからGPUテクスチャリソースを解決する。未ロードなら読み込み要求を行う
		const GPUTextureResource* Resolve(GraphicsCore& graphicsCore,
			AssetDatabase* assetDatabase, AssetID textureAssetID, bool sRGB = false);

	} // RuntimeTextureResolver
} // Engine
