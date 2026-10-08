#pragma once

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <type_traits>

namespace NEMTests {

	// 読取Bufferの書込操作をコンパイル時に検証する
	template <typename T>
	concept CanResizeBuffer = requires(T buffer) { buffer.Resize(1); };
	template <typename T>
	concept CanAddBufferElement = requires(T buffer) { buffer.Add(TestBufferElement{}); };
	template <typename T>
	concept CanEmplaceBufferElement = requires(T buffer) { buffer.EmplaceBack(); };
	template <typename T>
	concept CanClearBuffer = requires(T buffer) { buffer.Clear(); };
	template <typename T>
	concept CanReserveBuffer = requires(T buffer) { buffer.Reserve(1); };
	template <typename T>
	concept CanRemoveBufferElement = requires(T buffer) { buffer.RemoveAt(0); };
	template <typename T>
	concept CanSetBufferData = requires(T buffer) { buffer.SetData(nullptr, 0); };
	template <typename T>
	concept CanSetBufferElement = requires(T buffer) { buffer.SetElement(0, nullptr); };
	template <typename T>
	concept CanAssignBuffer = requires(T buffer) { buffer.Assign(std::span<const TestBufferElement>{}); };

	using ReadOnlyTestBuffer = Engine::DynamicBuffer<const TestBufferElement>;
	static_assert(!CanResizeBuffer<ReadOnlyTestBuffer> && !CanAddBufferElement<ReadOnlyTestBuffer> &&
		!CanEmplaceBufferElement<ReadOnlyTestBuffer> && !CanClearBuffer<ReadOnlyTestBuffer> &&
		!CanReserveBuffer<ReadOnlyTestBuffer> && !CanRemoveBufferElement<ReadOnlyTestBuffer> && !CanAssignBuffer<ReadOnlyTestBuffer>);
	static_assert(CanResizeBuffer<Engine::DynamicBuffer<TestBufferElement>>);
	static_assert(std::is_same_v<decltype(std::declval<ReadOnlyTestBuffer>().GetData()), const TestBufferElement*>);
	static_assert(std::is_same_v<decltype(std::declval<ReadOnlyTestBuffer>()[0]), const TestBufferElement&>);
	static_assert(std::is_same_v<decltype(std::declval<ReadOnlyTestBuffer>().GetSpan()), std::span<const TestBufferElement>>);
	static_assert(std::is_same_v<decltype(std::declval<Engine::ReadOnlyUntypedDynamicBuffer>().GetData()), const void*>);
	static_assert(!std::is_constructible_v<Engine::DynamicBuffer<TestBufferElement>, const Engine::DynamicBufferHeader*>);
	static_assert(!CanResizeBuffer<Engine::ReadOnlyUntypedDynamicBuffer> &&
		!CanRemoveBufferElement<Engine::ReadOnlyUntypedDynamicBuffer> && !CanSetBufferData<Engine::ReadOnlyUntypedDynamicBuffer> &&
		!CanSetBufferElement<Engine::ReadOnlyUntypedDynamicBuffer>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ECSWorld&>().TryGetUntypedBuffer({}, 0)),
		Engine::ReadOnlyUntypedDynamicBuffer>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ECSWorld&>().GetBuffer<TestBufferElement>({})),
		ReadOnlyTestBuffer>);
}
