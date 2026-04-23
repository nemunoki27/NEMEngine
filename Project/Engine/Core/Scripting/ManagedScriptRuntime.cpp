#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/System/Context/SystemContext.h>
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

// windows
#include <windows.h>
// c++
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>

//============================================================================
//	ManagedScriptRuntime classMethods
//============================================================================

namespace {

	// hostfxrのデリゲート種別
	constexpr int32_t kLoadAssemblyAndGetFunctionPointer = 5;

	// 現在のビルド設定名を返す
	std::string GetBuildProfile() {

		return _PROFILE;
	}

	// パスをUTF-8文字列へ変換する
	std::string ToUtf8Path(const std::filesystem::path& path) {

		return Engine::Algorithm::ConvertString(path.wstring());
	}

	// バージョン文字列を数値配列へ変換する
	std::array<int32_t, 4> ParseVersion(const std::wstring& text) {

		std::array<int32_t, 4> version{};
		size_t begin = 0;
		uint32_t index = 0;
		while (begin < text.size() && index < version.size()) {

			size_t end = text.find(L'.', begin);
			if (end == std::wstring::npos) {
				end = text.size();
			}

			int32_t value = 0;
			for (size_t i = begin; i < end; ++i) {
				if (text[i] < L'0' || L'9' < text[i]) {
					continue;
				}
				value = value * 10 + static_cast<int32_t>(text[i] - L'0');
			}
			version[index++] = value;

			begin = end + 1;
		}
		return version;
	}

	// バージョンが新しいか
	bool IsNewerVersion(const std::wstring& candidate, const std::wstring& current) {

		return ParseVersion(current) < ParseVersion(candidate);
	}

	// 環境変数からパスを取得する
	std::filesystem::path GetEnvironmentPath(const wchar_t* name) {

		wchar_t* value = nullptr;
		size_t length = 0;
		if (_wdupenv_s(&value, &length, name) != 0 || !value) {
			return {};
		}

		std::filesystem::path result = value;
		std::free(value);
		return result;
	}

	// dotnetルートを取得する
	std::filesystem::path FindDotnetRoot() {

		if (auto path = GetEnvironmentPath(L"DOTNET_ROOT_X64"); !path.empty()) {
			return path;
		}
		if (auto path = GetEnvironmentPath(L"DOTNET_ROOT"); !path.empty()) {
			return path;
		}
		return L"C:/Program Files/dotnet";
	}

	// hostfxr.dllを探す
	std::filesystem::path FindHostfxrPath() {

		const std::filesystem::path fxrRoot = FindDotnetRoot() / "host/fxr";
		if (!std::filesystem::exists(fxrRoot)) {
			return {};
		}

		std::filesystem::path bestPath{};
		std::wstring bestVersion{};
		for (const auto& entry : std::filesystem::directory_iterator(fxrRoot)) {

			if (!entry.is_directory()) {
				continue;
			}

			const std::wstring version = entry.path().filename().wstring();
			if (bestVersion.empty() || IsNewerVersion(version, bestVersion)) {
				bestVersion = version;
				bestPath = entry.path() / "hostfxr.dll";
			}
		}
		return std::filesystem::exists(bestPath) ? bestPath : std::filesystem::path{};
	}

	// 存在する最初のパスを返す
	std::filesystem::path FindFirstExistingPath(const std::vector<std::filesystem::path>& paths) {

		for (const auto& path : paths) {
			if (std::filesystem::exists(path)) {
				return path;
			}
		}
		return {};
	}

	// Engine側のScriptCoreを探す
	std::filesystem::path ResolveScriptCoreAssemblyPath() {

		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			current / "Managed/NEM.ScriptCore.dll",
			Engine::RuntimePaths::GetEngineLibraryRoot() / "Managed" / profile / "NEM.ScriptCore.dll"
			});
	}

	// ゲーム側スクリプトアセンブリを探す
	std::filesystem::path ResolveGameAssemblyPath() {

		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			current / "Managed/GameScripts.dll",
			Engine::RuntimePaths::GetGameRoot() / "Managed" / profile / "GameScripts.dll"
			});
	}

	// char配列からstd::stringへ変換する
	std::string MakeString(const char* text) {

		return text ? std::string(text) : std::string{};
	}

	// 名前空間付き型名からクラス名だけを取り出す
	std::string MakeSimpleTypeName(std::string_view typeName) {

		const size_t dot = typeName.find_last_of('.');
		if (dot == std::string_view::npos) {
			return std::string(typeName);
		}
		return std::string(typeName.substr(dot + 1));
	}

	// C#へ渡すEntityを作る
	Engine::ManagedNativeEntity MakeNativeEntity(Engine::ECSWorld& world, const Engine::Entity& entity) {

		Engine::ManagedNativeEntity native{};
		native.world = reinterpret_cast<std::uintptr_t>(&world);
		native.index = entity.index;
		native.generation = entity.generation;
		return native;
	}

	// C#から渡されたEntityを解決する
	Engine::ECSWorld* ResolveWorld(const Engine::ManagedNativeEntity& entity) {

		return reinterpret_cast<Engine::ECSWorld*>(entity.world);
	}

	Engine::Entity ResolveEntity(const Engine::ManagedNativeEntity& entity) {

		return Engine::Entity{ entity.index, entity.generation };
	}

	// Vector変換
	Engine::ManagedVector3 ToManagedVector3(const Engine::Vector3& value) {

		return Engine::ManagedVector3{ value.x, value.y, value.z };
	}

	Engine::Vector3 ToVector3(const Engine::ManagedVector3& value) {

		return Engine::Vector3{ value.x, value.y, value.z };
	}

	// Quaternion変換
	Engine::ManagedQuaternion ToManagedQuaternion(const Engine::Quaternion& value) {

		return Engine::ManagedQuaternion{ value.x, value.y, value.z, value.w };
	}

	Engine::Quaternion ToQuaternion(const Engine::ManagedQuaternion& value) {

		return Engine::Quaternion{ value.x, value.y, value.z, value.w };
	}

	// 親を考慮してワールド座標をローカル座標へ変換する
	Engine::Vector3 MakeLocalPositionFromWorld(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::Vector3& position) {

		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return position;
		}

		const Engine::Entity parent = world.GetComponent<Engine::HierarchyComponent>(entity).parent;
		if (!parent.IsValid() || !world.IsAlive(parent) || !world.HasComponent<Engine::TransformComponent>(parent)) {
			return position;
		}

		const Engine::Matrix4x4 inverseParent =
			Engine::Matrix4x4::Inverse(world.GetComponent<Engine::TransformComponent>(parent).worldMatrix);
		return Engine::Vector3::TransformPoint(position, inverseParent);
	}

	// トランスフォームを変更済みにする
	void MarkDirty(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}
		Engine::MarkTransformSubtreeDirty(world, entity);
	}
}

bool Engine::ManagedScriptRuntime::Init() {

	if (initialized_) {
		return true;
	}

	scriptCoreAssemblyPath_ = ResolveScriptCoreAssemblyPath();
	gameAssemblyPath_ = ResolveGameAssemblyPath();

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: ScriptCore path={}", ToUtf8Path(scriptCoreAssemblyPath_));
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: GameScripts path={}", ToUtf8Path(gameAssemblyPath_));

	if (scriptCoreAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: NEM.ScriptCore.dll was not found.");
		return false;
	}

	if (!LoadHostfxr() || !InitRuntime() || !LoadBridgeFunctions()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to initialize .NET host bridge.");
		Finalize();
		return false;
	}

	ManagedNativeApiTable callbacks{};
	callbacks.getDeltaTime = &ManagedScriptRuntime::GetDeltaTimeCallback;
	callbacks.getPosition = &ManagedScriptRuntime::GetPositionCallback;
	callbacks.setPosition = &ManagedScriptRuntime::SetPositionCallback;
	callbacks.getLocalPosition = &ManagedScriptRuntime::GetLocalPositionCallback;
	callbacks.setLocalPosition = &ManagedScriptRuntime::SetLocalPositionCallback;
	callbacks.getLocalScale = &ManagedScriptRuntime::GetLocalScaleCallback;
	callbacks.setLocalScale = &ManagedScriptRuntime::SetLocalScaleCallback;
	callbacks.getLocalRotation = &ManagedScriptRuntime::GetLocalRotationCallback;
	callbacks.setLocalRotation = &ManagedScriptRuntime::SetLocalRotationCallback;

	if (!initializeNativeApi_ || initializeNativeApi_(&callbacks) == 0) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to initialize native callbacks.");
		Finalize();
		return false;
	}

	initialized_ = true;
	if (!LoadGameAssembly()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dll was not loaded. Managed scripts will be unavailable.");
	}
	RefreshScriptTypes();
	return true;
}

void Engine::ManagedScriptRuntime::Finalize() {

	fieldCache_.clear();
	currentContext_ = nullptr;
	initialized_ = false;

	initializeNativeApi_ = nullptr;
	loadGameAssembly_ = nullptr;
	getScriptTypeCount_ = nullptr;
	copyScriptTypeName_ = nullptr;
	getSerializedFieldCount_ = nullptr;
	copySerializedFieldInfo_ = nullptr;
	createInstance_ = nullptr;
	destroyInstance_ = nullptr;
	invokeAwake_ = nullptr;
	invokeStart_ = nullptr;
	invokeOnEnable_ = nullptr;
	invokeOnDisable_ = nullptr;
	invokeOnDestroy_ = nullptr;
	invokeFixedUpdate_ = nullptr;
	invokeUpdate_ = nullptr;
	invokeLateUpdate_ = nullptr;

	ReleaseHostfxr();
}

void Engine::ManagedScriptRuntime::RefreshScriptTypes() {

	if (!initialized_ || !getScriptTypeCount_ || !copyScriptTypeName_) {
		return;
	}

	const int32_t typeCount = getScriptTypeCount_();
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: managed script type count={}", typeCount);
	for (int32_t i = 0; i < typeCount; ++i) {

		char name[256]{};
		if (copyScriptTypeName_(i, name, static_cast<int32_t>(sizeof(name))) <= 0) {
			continue;
		}
		BehaviorTypeRegistry::GetInstance().RegisterManaged(name);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptRuntime: registered managed script type={}", name);
	}
}

int32_t Engine::ManagedScriptRuntime::CreateInstance(const std::string& typeName,
	ECSWorld& world, const Entity& entity, const nlohmann::json& serializedFields) {

	if (!initialized_ || !createInstance_) {
		return 0;
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	return createInstance_(typeName.c_str(), MakeNativeEntity(world, entity), json.c_str());
}

void Engine::ManagedScriptRuntime::DestroyInstance(int32_t handle) {

	if (!initialized_ || !destroyInstance_ || handle == 0) {
		return;
	}
	destroyInstance_(handle);
}

void Engine::ManagedScriptRuntime::InvokeAwake(int32_t handle, const SystemContext& context) {

	Invoke(invokeAwake_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeStart(int32_t handle, const SystemContext& context) {

	Invoke(invokeStart_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeOnEnable(int32_t handle, const SystemContext& context) {

	Invoke(invokeOnEnable_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeOnDisable(int32_t handle, const SystemContext& context) {

	Invoke(invokeOnDisable_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeOnDestroy(int32_t handle, const SystemContext& context) {

	Invoke(invokeOnDestroy_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeFixedUpdate(int32_t handle, const SystemContext& context) {

	Invoke(invokeFixedUpdate_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeUpdate(int32_t handle, const SystemContext& context) {

	Invoke(invokeUpdate_, handle, context);
}

void Engine::ManagedScriptRuntime::InvokeLateUpdate(int32_t handle, const SystemContext& context) {

	Invoke(invokeLateUpdate_, handle, context);
}

const std::vector<Engine::ManagedScriptField>& Engine::ManagedScriptRuntime::GetSerializedFields(const std::string& typeName) {

	static const std::vector<ManagedScriptField> kEmpty{};

	if (auto it = fieldCache_.find(typeName); it != fieldCache_.end()) {
		return it->second;
	}
	if (!initialized_ || !getSerializedFieldCount_ || !copySerializedFieldInfo_) {
		return kEmpty;
	}

	const int32_t fieldCount = getSerializedFieldCount_(typeName.c_str());
	std::vector<ManagedScriptField> fields{};
	fields.reserve(std::max(0, fieldCount));

	for (int32_t i = 0; i < fieldCount; ++i) {

		ManagedNativeSerializedFieldInfo nativeInfo{};
		if (copySerializedFieldInfo_(typeName.c_str(), i, &nativeInfo) == 0) {
			continue;
		}

		ManagedScriptField field{};
		field.name = MakeString(nativeInfo.name);
		field.displayName = MakeString(nativeInfo.displayName);
		field.kind = static_cast<ManagedSerializedFieldKind>(nativeInfo.kind);
		field.isPublic = nativeInfo.isPublic != 0;
		field.defaultValueJson = MakeString(nativeInfo.defaultValueJson);
		fields.emplace_back(std::move(field));
	}

	auto [it, inserted] = fieldCache_.emplace(typeName, std::move(fields));
	return inserted ? it->second : kEmpty;
}

bool Engine::ManagedScriptRuntime::TryResolveScriptTypeName(const std::string_view& scriptName, std::string& outTypeName) const {

	const std::string simpleName = MakeSimpleTypeName(scriptName);
	const auto& registry = BehaviorTypeRegistry::GetInstance();

	// まず完全一致を優先して、名前空間を含む指定でも曖昧にならないようにする
	for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

		const auto& info = registry.GetInfo(i);
		if (!info.managed) {
			continue;
		}
		if (info.name == scriptName) {
			outTypeName = info.name;
			return true;
		}
	}

	// アセット名は通常ファイル名なので、C#側のクラス名と照合する
	for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

		const auto& info = registry.GetInfo(i);
		if (!info.managed) {
			continue;
		}
		if (MakeSimpleTypeName(info.name) == simpleName) {
			outTypeName = info.name;
			return true;
		}
	}
	return false;
}

Engine::ManagedScriptRuntime& Engine::ManagedScriptRuntime::GetInstance() {

	static ManagedScriptRuntime runtime;
	return runtime;
}

bool Engine::ManagedScriptRuntime::LoadHostfxr() {

	const std::filesystem::path hostfxrPath = FindHostfxrPath();
	if (hostfxrPath.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr.dll was not found.");
		return false;
	}

	hostfxrLibrary_ = ::LoadLibraryW(hostfxrPath.c_str());
	if (!hostfxrLibrary_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to load hostfxr.dll.");
		return false;
	}

	auto loadFunction = [&](const char* name) -> void* {
		return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(hostfxrLibrary_), name));
		};

	hostfxrClose_ = reinterpret_cast<HostfxrCloseFn>(loadFunction("hostfxr_close"));
	auto initializeForRuntimeConfig =
		reinterpret_cast<HostfxrInitializeForRuntimeConfigFn>(loadFunction("hostfxr_initialize_for_runtime_config"));
	auto getRuntimeDelegate =
		reinterpret_cast<HostfxrGetRuntimeDelegateFn>(loadFunction("hostfxr_get_runtime_delegate"));

	if (!hostfxrClose_ || !initializeForRuntimeConfig || !getRuntimeDelegate) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr exports were not found.");
		return false;
	}

	HostfxrHandle context = nullptr;
	const std::filesystem::path runtimeConfigPath =
		scriptCoreAssemblyPath_.parent_path() / "NEM.ScriptCore.runtimeconfig.json";
	if (!std::filesystem::exists(runtimeConfigPath)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: runtimeconfig was not found. path={}", ToUtf8Path(runtimeConfigPath));
		return false;
	}

	int32_t result = initializeForRuntimeConfig(runtimeConfigPath.c_str(), nullptr, &context);
	if (result != 0 || !context) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: hostfxr_initialize_for_runtime_config failed. code={}", result);
		return false;
	}

	result = getRuntimeDelegate(context, kLoadAssemblyAndGetFunctionPointer,
		reinterpret_cast<void**>(&loadAssemblyAndGetFunctionPointer_));
	hostfxrClose_(context);

	if (result != 0 || !loadAssemblyAndGetFunctionPointer_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to get load_assembly_and_get_function_pointer. code={}", result);
		return false;
	}
	return true;
}

bool Engine::ManagedScriptRuntime::InitRuntime() {

	return loadAssemblyAndGetFunctionPointer_ != nullptr;
}

bool Engine::ManagedScriptRuntime::LoadBridgeFunctions() {

	bool success = true;
	success &= LoadBridgeFunction(initializeNativeApi_, L"InitializeNativeApi");
	success &= LoadBridgeFunction(loadGameAssembly_, L"LoadGameAssembly");
	success &= LoadBridgeFunction(getScriptTypeCount_, L"GetScriptTypeCount");
	success &= LoadBridgeFunction(copyScriptTypeName_, L"CopyScriptTypeName");
	success &= LoadBridgeFunction(getSerializedFieldCount_, L"GetSerializedFieldCount");
	success &= LoadBridgeFunction(copySerializedFieldInfo_, L"CopySerializedFieldInfo");
	success &= LoadBridgeFunction(createInstance_, L"CreateInstance");
	success &= LoadBridgeFunction(destroyInstance_, L"DestroyInstance");
	success &= LoadBridgeFunction(invokeAwake_, L"InvokeAwake");
	success &= LoadBridgeFunction(invokeStart_, L"InvokeStart");
	success &= LoadBridgeFunction(invokeOnEnable_, L"InvokeOnEnable");
	success &= LoadBridgeFunction(invokeOnDisable_, L"InvokeOnDisable");
	success &= LoadBridgeFunction(invokeOnDestroy_, L"InvokeOnDestroy");
	success &= LoadBridgeFunction(invokeFixedUpdate_, L"InvokeFixedUpdate");
	success &= LoadBridgeFunction(invokeUpdate_, L"InvokeUpdate");
	success &= LoadBridgeFunction(invokeLateUpdate_, L"InvokeLateUpdate");
	return success;
}

bool Engine::ManagedScriptRuntime::LoadGameAssembly() {

	if (gameAssemblyPath_.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptRuntime: GameScripts.dll was not found.");
		return false;
	}
	if (!loadGameAssembly_) {
		return false;
	}

	const std::string path = ToUtf8Path(gameAssemblyPath_);
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptRuntime: loading GameScripts.dll from {}", path);
	if (loadGameAssembly_(path.c_str()) == 0) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptRuntime: failed to load GameScripts.dll. path={}", path);
		return false;
	}
	return true;
}

void Engine::ManagedScriptRuntime::ReleaseHostfxr() {

	loadAssemblyAndGetFunctionPointer_ = nullptr;
	hostfxrClose_ = nullptr;

	if (hostfxrLibrary_) {
		::FreeLibrary(static_cast<HMODULE>(hostfxrLibrary_));
		hostfxrLibrary_ = nullptr;
	}
}

void Engine::ManagedScriptRuntime::Invoke(InvokeFn function, int32_t handle, const SystemContext& context) {

	if (!initialized_ || !function || handle == 0) {
		return;
	}

	currentContext_ = &context;
	function(handle);
	currentContext_ = nullptr;
}

float Engine::ManagedScriptRuntime::GetDeltaTimeCallback() {

	const SystemContext* context = GetInstance().currentContext_;
	return context ? context->deltaTime : 0.0f;
}

Engine::ManagedVector3 Engine::ManagedScriptRuntime::GetPositionCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return {};
	}
	return ToManagedVector3(world->GetComponent<TransformComponent>(resolved).worldMatrix.GetTranslationValue());
}

void Engine::ManagedScriptRuntime::SetPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	auto& transform = world->GetComponent<TransformComponent>(resolved);
	transform.localPos = MakeLocalPositionFromWorld(*world, resolved, ToVector3(value));
	MarkDirty(*world, resolved);
}

Engine::ManagedVector3 Engine::ManagedScriptRuntime::GetLocalPositionCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return {};
	}
	return ToManagedVector3(world->GetComponent<TransformComponent>(resolved).localPos);
}

void Engine::ManagedScriptRuntime::SetLocalPositionCallback(ManagedNativeEntity entity, ManagedVector3 value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	world->GetComponent<TransformComponent>(resolved).localPos = ToVector3(value);
	MarkDirty(*world, resolved);
}

Engine::ManagedVector3 Engine::ManagedScriptRuntime::GetLocalScaleCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return ManagedVector3{ 1.0f, 1.0f, 1.0f };
	}
	return ToManagedVector3(world->GetComponent<TransformComponent>(resolved).localScale);
}

void Engine::ManagedScriptRuntime::SetLocalScaleCallback(ManagedNativeEntity entity, ManagedVector3 value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	world->GetComponent<TransformComponent>(resolved).localScale = ToVector3(value);
	MarkDirty(*world, resolved);
}

Engine::ManagedQuaternion Engine::ManagedScriptRuntime::GetLocalRotationCallback(ManagedNativeEntity entity) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return {};
	}
	return ToManagedQuaternion(world->GetComponent<TransformComponent>(resolved).localRotation);
}

void Engine::ManagedScriptRuntime::SetLocalRotationCallback(ManagedNativeEntity entity, ManagedQuaternion value) {

	ECSWorld* world = ResolveWorld(entity);
	const Entity resolved = ResolveEntity(entity);
	if (!world || !world->IsAlive(resolved) || !world->HasComponent<TransformComponent>(resolved)) {
		return;
	}

	world->GetComponent<TransformComponent>(resolved).localRotation = Quaternion::Normalize(ToQuaternion(value));
	MarkDirty(*world, resolved);
}
