#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/UI/Inspectors/Core/IInspectorComponentDrawer.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

	class ECSWorld;

	//============================================================================
	//	ComponentEditorDescriptor structure
	//	Inspectorで扱うコンポーネント編集情報をまとめる
	//============================================================================
	struct ComponentEditorDescriptor {

		// メニュー表示名
		std::string menuLabel;
		// コンポーネント登録名
		std::string typeName;
		// メニュー分類名
		std::string category;
		// 複数追加を許可するか
		bool allowMultiple = false;
		// 追加削除メニューに表示するか
		bool showInComponentMenu = true;
		// 一括削除メニューに表示するか
		bool showInRemoveMenu = true;

		// 描画処理の生成関数
		std::function<std::unique_ptr<IInspectorComponentDrawer>()> drawerFactory{};
		// 追加コマンドの生成関数
		std::function<std::unique_ptr<IEditorCommand>(const Entity& entity)> addCommandFactory{};
	};

	//============================================================================
	//	ComponentEditorRegistry class
	//	Inspectorのコンポーネント編集情報を登録する
	//============================================================================
	class ComponentEditorRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ComponentEditorRegistry() = default;
		~ComponentEditorRegistry();

		// コンポーネント編集情報を登録する
		IInspectorComponentDrawer* Register(ComponentEditorDescriptor descriptor);
		// 指定コンポーネントを追加できるか
		bool CanAdd(const ComponentEditorDescriptor& descriptor, ECSWorld& world, const Entity& entity) const;
		// 追加コマンドを生成する
		std::unique_ptr<IEditorCommand> CreateAddCommand(
			const ComponentEditorDescriptor& descriptor, const Entity& entity) const;

		//--------- accessor -----------------------------------------------------

		// 登録された編集情報を取得する
		const std::vector<ComponentEditorDescriptor>& GetDescriptors() const { return descriptors_; }
		// 登録された描画処理を取得する
		const std::vector<std::unique_ptr<IInspectorComponentDrawer>>& GetDrawers() const { return drawers_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// 指定typeNameが登録済みか
		bool HasDescriptor(const std::string& typeName) const;

		//--------- variables ----------------------------------------------------

		// 登録された編集情報
		std::vector<ComponentEditorDescriptor> descriptors_{};
		// 登録された描画処理
		std::vector<std::unique_ptr<IInspectorComponentDrawer>> drawers_{};
	};
} // Engine
