#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Physics/Collision/CollisionTypes.h>

// c++
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	CollisionSettings class
	//	Collisionタイプと衝突マトリクスを保存、管理するクラス
	//============================================================================
	class CollisionSettings {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		CollisionSettings() = default;
		~CollisionSettings() = default;

		// 設定が未読み込みなら読み込む
		void EnsureLoaded();
		// 設定ファイルからCollision設定を読み込む
		void Load();
		// 現在のCollision設定を設定ファイルへ保存する
		void Save() const;
		// 使用するCollision設定ファイルをアセットGUIDから切り替える
		void SetActiveSettingsAsset(AssetID assetID, const AssetDatabase* assetDatabase);
		// 使用するCollision設定ファイルを実ファイルパスから切り替える
		void SetActiveSettingsPath(const std::filesystem::path& settingsPath);

		// Collisionタイプを追加する
		bool AddType(const std::string& name);
		// 最後のCollisionタイプを削除する
		void RemoveLastType();
		// Collisionタイプ名を変更する
		void SetTypeName(uint32_t index, const std::string& name);
		// Collisionタイプの有効状態を変更する
		void SetTypeEnabled(uint32_t index, bool enabled);
		// Collisionタイプ同士の衝突可否を変更する
		void SetPairEnabled(uint32_t typeA, uint32_t typeB, bool enabled);

		// Collisionタイプ同士が衝突可能か
		bool IsPairEnabled(uint32_t typeA, uint32_t typeB) const;
		// Collisionタイプマスク同士が衝突可能か
		bool CanCollide(uint32_t typeMaskA, uint32_t typeMaskB) const;

		//--------- accessor -----------------------------------------------------

		// Collisionタイプ一覧を取得する
		const std::vector<CollisionTypeDefinition>& GetTypes() const { return types_; }
		// Collisionタイプ数を取得する
		uint32_t GetTypeCount() const { return static_cast<uint32_t>(types_.size()); }
		// 現在使用している設定ファイルパスを取得する
		const std::filesystem::path& GetSettingsPath() const { return settingsPath_; }

		// シングルトンインスタンスを取得する
		static CollisionSettings& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 読み込み済みフラグ
		bool loaded_ = false;

		// 設定ファイルパス
		std::filesystem::path settingsPath_{};

		// Collisionタイプ一覧
		std::vector<CollisionTypeDefinition> types_{};

		// UnityのLayer Collision Matrix相当のビット行
		std::array<uint32_t, kMaxCollisionTypes> matrixRows_{};

		//--------- functions ----------------------------------------------------

		// デフォルト設定に戻す
		void ResetDefault();
		// タイプ数に合わせてマトリクスを切り詰める
		void TrimMatrix();
	};
}

