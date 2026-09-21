#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <vector>
#include <span>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	EditorSelectionOutlineRequestService class
	// エディタ選択アウトラインのtemporary request置き場
	// LineRendererと同様、フレームごとに要求を積み、描画パスが消費する
	// 選択EntityへComponentを追加せず、Scene保存にも混入しない
	//============================================================================
	class EditorSelectionOutlineRequestService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		static EditorSelectionOutlineRequestService& GetInstance();

		// フレーム開始時にtemporary requestをクリアする
		void BeginFrame() { requests_.clear(); }

		// 選択アウトライン要求を積み、subMeshIndexが負ならEntity全体
		void Request(ECSWorld* world, const Entity& entity, int32_t subMeshIndex,
			const ScreenSpaceOutlineStyle& style);

		//--------- accessor -----------------------------------------------------

		std::span<const ScreenSpaceOutlineRequest> GetRequests() const { return requests_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 複数選択にも対応できるようvectorで保持する
		std::vector<ScreenSpaceOutlineRequest> requests_;
	};
} // Engine

