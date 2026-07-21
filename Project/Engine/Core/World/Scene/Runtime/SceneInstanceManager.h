#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <cstdint>

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
		//============================================================================
		//	public Methods
		//============================================================================

		SceneInstanceManager() = default;
		~SceneInstanceManager() = default;

		// シーンをロードしてインスタンスを作成し、forcedInstanceIDが有効ならそのinstance IDを使ってC#側で先行採番したSceneHandleと一致させる
		bool LoadAdditive(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world, AssetID sceneAsset, UUID forcedInstanceID = UUID{});
		// 単一シーン読み込み要求を予約する
		bool TryBeginSingleLoadRequest();
		// 単一シーン読み込み要求の予約を解除する
		void ClearSingleLoadRequest();
		// シーンアセットを持たない一時シーンインスタンスを作成してアクティブにする、プレファブ編集の隔離ワールド用
		// headerは環境(スカイボックス/ライティング等)の流用元、新規IDを採番して返す
		UUID CreateScratchScene(const SceneHeader& header);
		// シーンインスタンスをアンロード
		bool Unload(ECSWorld& world, UUID instanceID);
		// 全てのシーンインスタンスをアンロード
		void UnloadAll(ECSWorld& world);
		// アクティブなシーンをファイルに保存する
		bool SaveActive(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world) const;

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
		const SceneInstance* FindChildBySlot(UUID parentID, const std::string_view& slotName) const;

		// アクティブなシーンインスタンスを取得する
		const SceneInstance* GetActive() const;
		// 全てのシーンインスタンスのリストを取得する
		const std::vector<SceneInstance>& GetAll() const { return scenes_; }
		// シーン構成の変更番号を取得する
		uint64_t GetRevision() const { return revision_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// アクティブなシーンインスタンスのID
		UUID active_{};
		// 単一シーン読み込み要求を処理中か
		bool singleLoadRequestPending_ = false;
		// シーンインスタンスのリスト
		std::vector<SceneInstance> scenes_;
		// シーンの追加、削除、アクティブ変更ごとに進む番号
		uint64_t revision_ = 0;

		//--------- functions ----------------------------------------------------

		// シーンインスタンスが所持しているエンティティを収集する
		static std::vector<Entity> CollectSceneEntities(ECSWorld& world, const SceneInstance& scene);
	};
} // Engine
