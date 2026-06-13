#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <vector>

// assimp前方宣言で、ヘッダではassimpを引き込まない
struct aiScene;
struct aiNode;

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	ModelPreviewUtility namespace
	//	ProjectPanelとInspectorPanelで共有するモデルサムネイルプレビュー用の補助関数
	//============================================================================
	namespace ModelPreviewUtility {

		// モデルのノード階層を辿って全頂点位置をエンジン座標で集める
		void CollectNodePositions(const aiScene* scene, const aiNode* node, std::vector<Vector3>& outPositions);

		// モデル全体を収めるためのプレビューカメラ距離を計算する
		float CalculateCameraDistance(const Vector3& min, const Vector3& max, const Vector3& center,
			float pitchDegrees, float yawDegrees, float fovYDegrees, float aspectRatio, float distanceScale);

		// モデルが参照するテクスチャをAssetDatabaseへインポートする
		void ImportReferencedTextures(AssetDatabase& database, AssetID meshAssetID);

		// モデルの頂点境界(min/max/center/radius)を計算する、メッシュが読めれば結果を書いてtrueを返す
		bool ComputeBounds(const AssetDatabase& database, AssetID meshAssetID,
			Vector3& outMin, Vector3& outMax, Vector3& outCenter, float& outRadius);
	} // ModelPreviewUtility
} // Engine
