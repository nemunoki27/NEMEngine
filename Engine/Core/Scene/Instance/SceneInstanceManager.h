#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scene/Header/SceneHeader.h>
#include <Engine/Core/Scene/SceneSystem.h>
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Utility/Json/JsonAdapter.h>

namespace Engine {

	//============================================================================
	//	SceneInstanceManager structures
	//============================================================================

	// シーン内の子シーンのリンク情報
	struct SceneChildLink {

		// 子シーンのスロット名
		std::string slotName;
		// 子シーンのインスタンスID
		UUID childInstanceID{};
	};

	// シーンインスタンスの情報
	struct SceneInstance {

		// インスタンスID
		UUID instanceID{};
		// 親シーンのインスタンスID
		UUID parentInstanceID{};
		// シーンアセットID
		AssetID sceneAsset{};

		// シーンの所持するヘッダー情報
		SceneHeader header{};
		// シーン内で作成されたエンティティリスト
		std::vector<Entity> createdEntities;
		// 子シーンのリンク情報リスト
		std::vector<SceneChildLink> childScenes;
	};

	//============================================================================
	//	SceneInstanceManager class
	//	シーン内のインスタンスを管理するクラス
	//============================================================================
	class SceneInstanceManager {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneInstanceManager() = default;
		~SceneInstanceManager() = default;

		// シーンをロードしてインスタンスを作成
		bool LoadAdditive(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world, AssetID sceneAsset);
		// シーンインスタンスをアンロード
		bool Unload(ECSWorld& world, UUID instanceID);

		// シーンの処理を開始するときのスナップショット
		nlohmann::json SerializeSnapshot(const SceneSystem& sceneSystem, ECSWorld& world) const;
		bool LoadSnapshot(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world, const nlohmann::json& snapshot);
		bool LoadSceneTree(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world, AssetID rootAsset);

		//--------- accessor -----------------------------------------------------

		// アクティブなシーンインスタンスを切り替える
		void SetActive(UUID instanceID);

		// UUIDからシーンインスタンスを検索する
		const SceneInstance* Find(UUID id) const;
		SceneInstance* Find(UUID id);
		const SceneInstance* FindChildBySlot(UUID parentId, const std::string_view& slotName) const;

		// アクティブなシーンインスタンスを取得する
		const SceneInstance* GetActive() const;
		// 全てのシーンインスタンスのリストを取得する
		const std::vector<SceneInstance>& GetAll() const { return scenes_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// アクティブなシーンインスタンスのID
		UUID active_{};
		// シーンインスタンスのリスト
		std::vector<SceneInstance> scenes_;
	};
} // Engine