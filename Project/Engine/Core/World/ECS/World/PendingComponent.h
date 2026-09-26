#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/AlignedBuffer.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>

namespace Engine {

	//============================================================================
	//	PendingComponent class
	//	安全地点で追加するComponentの値と個体番号を保持する
	//============================================================================
	class PendingComponent {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PendingComponent(const ComponentTypeInfo& info, uint64_t instanceID);
		~PendingComponent();
		PendingComponent(const PendingComponent&) = delete;
		PendingComponent& operator=(const PendingComponent&) = delete;

		// 未適用の追加を取り消して値を解放する
		void Cancel();

		//--------- accessor -----------------------------------------------------

		const ComponentTypeInfo& GetInfo() const { return info_; }
		uint64_t GetInstanceID() const { return instanceID_; }
		void* GetData() { return data_.ptr; }
		const void* GetData() const { return data_.ptr; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const ComponentTypeInfo& info_;
		uint64_t instanceID_;
		AlignedBuffer data_;
	};
}
