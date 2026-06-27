#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <functional>
#include <string>
#include <vector>
// imgui
#include <imgui.h>

namespace Engine {

	// 前方宣言
	struct EditorPanelContext;
	class ProjectAssetThumbnailCache;
	struct ProjectAssetEntry;

	//============================================================================
	//	AssetActionDescriptor structure
	//	ProjectPanelでのアセット種別ごとの操作をまとめる
	//============================================================================
	struct AssetActionDescriptor {

		// 対象アセット種別
		AssetType type = AssetType::Unknown;
		// 表示名
		std::string displayName;
		// アイコン解決処理
		std::function<ImTextureID(ProjectAssetThumbnailCache&, const ProjectAssetEntry&)> iconResolver{};
		// ダブルクリック時の処理
		std::function<void(const EditorPanelContext&, const ProjectAssetEntry&)> onDoubleClick{};
		// 右クリックメニューへ種別固有の項目を追加する処理
		std::function<void(const EditorPanelContext&, const ProjectAssetEntry&)> onContextMenu{};
		// ドラッグ開始時の処理
		std::function<void(const ProjectAssetEntry&, ImGuiDragDropFlags)> onDragSource{};
	};

	//============================================================================
	//	AssetActionRegistry class
	//	アセット種別ごとの操作を登録し種別から引く
	//============================================================================
	class AssetActionRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetActionRegistry() = default;
		~AssetActionRegistry() = default;

		// アセット操作を登録する
		bool Register(AssetActionDescriptor descriptor);
		// 種別から操作を取得する、無ければnullptr
		const AssetActionDescriptor* Find(AssetType type) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 登録された操作、種別ごとに一意
		std::vector<AssetActionDescriptor> descriptors_{};

		//--------- functions ----------------------------------------------------

		// 指定AssetTypeが登録済みか
		bool HasDescriptor(AssetType type) const;
	};
} // Engine
