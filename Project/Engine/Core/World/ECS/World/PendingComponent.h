#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/AlignedBuffer.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>

// c++
#include <memory>

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

		// 未適用の追加を取得対象から外す
		void Cancel();
		// 処理中の値を取消後も保持する
		std::shared_ptr<const void> AcquireValueLease() const { return value_; }

		//--------- accessor -----------------------------------------------------

		const ComponentTypeInfo& GetInfo() const { return info_; }
		uint64_t GetInstanceID() const { return instanceID_; }
		void* GetData() { return value_ ? value_->data.GetData() : nullptr; }
		const void* GetData() const { return value_ ? value_->data.GetData() : nullptr; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 構築済みの値を借用処理の終了まで所有する
		struct ValueState {

			explicit ValueState(const ComponentTypeInfo& info);
			~ValueState();
			ValueState(const ValueState&) = delete;
			ValueState& operator=(const ValueState&) = delete;

			const ComponentTypeInfo& info;
			AlignedBuffer data;
		};

		//--------- variables ----------------------------------------------------

		const ComponentTypeInfo& info_;
		uint64_t instanceID_;
		std::shared_ptr<ValueState> value_;
	};
}
