#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/World/ECS/Entity/WorldEntityKey.h>

// c++
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RenderTransformHistory class
	//	Viewと描画インスタンスごとに直前の描画姿勢を保持する
	//============================================================================
	class RenderTransformHistory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 前回の描画姿勢を返し、現在の姿勢を記録する
		Matrix4x4 Update(const std::string& view, WorldEntityKey owner, uint64_t instanceID,
			uint64_t frameSerial, const Matrix4x4& current);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct InstanceKey {

			WorldEntityKey owner;
			uint64_t instanceID = 0;
			bool operator==(const InstanceKey&) const = default;
		};
		struct InstanceKeyHash {

			size_t operator()(const InstanceKey& key) const;
		};
		struct Frame {

			Matrix4x4 current = Matrix4x4::Identity();
			Matrix4x4 previous = Matrix4x4::Identity();
			uint64_t serial = 0;
			bool initialized = false;
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, std::unordered_map<InstanceKey, Frame, InstanceKeyHash>> views_;
		uint64_t collectedFrame_ = 0;

		//--------- functions ----------------------------------------------------

		// 描画されなくなったインスタンスとViewを回収する
		void Collect(uint64_t frameSerial);
	};
}
