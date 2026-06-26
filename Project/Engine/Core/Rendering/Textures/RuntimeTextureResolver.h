#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

namespace Engine {

	class GraphicsCore;

	namespace RuntimeTextureResolver {

		// アセットIDからGPUテクスチャリソースを解決し未ロードなら読み込み要求を行う
		const GPUTextureResource* Resolve(GraphicsCore& graphicsCore,
			AssetDatabase* assetDatabase, AssetID textureAssetID, bool sRGB = false);

		// テクスチャの実ピクセルサイズを取得する、ロード済みのときのみtrueを返しフォールバックは対象外
		bool TryResolveSize(GraphicsCore& graphicsCore,
			AssetDatabase* assetDatabase, AssetID textureAssetID, Vector2& outSize);

	} // RuntimeTextureResolver
} // Engine
