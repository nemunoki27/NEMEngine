#include "BehaviorSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/ScriptComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>

//============================================================================
//	BehaviorSystem classMethods
//============================================================================

namespace {

	// タイプ名からビヘイビアの型IDを取得する
	bool TryResolveTypeID(uint32_t& outTypeID, const std::string& typeName) {

		if (typeName.empty()) {
			return false;
		}
		const auto* info = Engine::BehaviorTypeRegistry::GetInstance().FindByName(typeName);
		if (!info) {
			return false;
		}
		// 参照に渡してtrueを返す
		outTypeID = info->id;
		return true;
	}
}

void Engine::BehaviorSystem::OnWorldEnter(ECSWorld& world, SystemContext& context) {

	// ワールドをアクティブにする
	EnsureActiveWorld(world, context);

	// プレイモードでワールドに入ったときは、スクリプトのビヘイビアの実体化と初期化を行う
	if (context.mode == WorldMode::Play) {

		Prepare(world, context, false);
	}
}

void Engine::BehaviorSystem::OnWorldExit(ECSWorld& world, SystemContext& context) {

	// ワールドがアクティブでないなら何もしない
	if (activeWorld_ != &world) {
		return;
	}
	// ワールドを破棄する
	runtime_.DestroyAll(world, context);
	activeWorld_ = nullptr;
}

void Engine::BehaviorSystem::FixedUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}

	// 処理前準備
	Prepare(world, context, false);

	runtime_.ForEachAlive([&](BehaviorRecord& record) {

		// 無効なインスタンスは処理しない
		if (!record.enabled || !record.instance) {
			return;
		}
		record.instance->FixedUpdate(world, context, record.owner);
		});
}

void Engine::BehaviorSystem::Update(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}

	// 処理前準備
	Prepare(world, context, true);

	runtime_.ForEachAlive([&](BehaviorRecord& record) {

		// 無効なインスタンスは処理しない
		if (!record.enabled || !record.instance) {
			return;
		}
		record.instance->Update(world, context, record.owner);
		});
}

void Engine::BehaviorSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}

	// 処理前準備
	Prepare(world, context, false);

	runtime_.ForEachAlive([&](BehaviorRecord& record) {

		// 無効なインスタンスは処理しない
		if (!record.enabled || !record.instance) {
			return;
		}
		record.instance->LateUpdate(world, context, record.owner);
		});
}

void Engine::BehaviorSystem::EnsureActiveWorld(ECSWorld& world, SystemContext& context) {

	// プレイ中でないときにアクティブにしない
	if (context.mode != WorldMode::Play) {
		activeWorld_ = nullptr;
		return;
	}
	// すでにアクティブなワールドなら何もしない
	if (activeWorld_ == &world) {
		return;
	}

	// 前のワールドを破棄する
	if (activeWorld_) {

		runtime_.DestroyAll(*activeWorld_, context);
	}

	// 新しいワールドをアクティブにする
	activeWorld_ = &world;
	ResetRuntimeState(world);
}

void Engine::BehaviorSystem::ResetRuntimeState(ECSWorld& world) {

	// スクリプトコンポーネントを持つ全てのエンティティに対して、スクリプトのビヘイビアの実体化と初期化を行う
	world.ForEach<ScriptComponent>([&](Entity, ScriptComponent& component) {
		for (auto& entry : component.scripts) {

			entry.handle = BehaviorHandle::Null();
		}
		});
}

void Engine::BehaviorSystem::Prepare(ECSWorld& world, SystemContext& context, bool doSweep) {

	// ワールドを設定
	EnsureActiveWorld(world, context);
	// プレイモードでない、もしくはアクティブなワールドでないときは何もしない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// フラグリセット
	runtime_.ClearSeenFlags();

	// スクリプトコンポーネントを持つ全てのエンティティに対して、スクリプトのビヘイビアの実体化と初期化を行う
	world.ForEach<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
		for (auto& entry : component.scripts) {

			// タイプ名が空ならビヘイビアを破棄して無効にする
			if (entry.type.empty()) {
				if (entry.handle.IsValid()) {

					runtime_.Destroy(entry.handle, world, context);
					entry.handle = BehaviorHandle::Null();
				}
				continue;
			}

			uint32_t typeID = 0;
			if (!TryResolveTypeID(typeID, entry.type)) {
				continue;
			}

			// ハンドルが有効で、かつ生存しているならレコードを取得する
			BehaviorRecord* record = nullptr;
			if (runtime_.IsAlive(entry.handle)) {

				record = runtime_.GetRecord(entry.handle);
				// 無効の場合はハンドルを破棄して無効にする
				if (!record || record->typeID != typeID || record->owner != entity) {

					runtime_.Destroy(entry.handle, world, context);
					entry.handle = BehaviorHandle::Null();
					record = nullptr;
				}
			}
			// ハンドルが無効なら新しくビヘイビアを生成する
			if (!record) {

				entry.handle = runtime_.Create(typeID, entity);
				record = runtime_.GetRecord(entry.handle);
				// 生成に失敗している場合はハンドルを破棄して無効にする
				if (!record || !record->instance) {
					entry.handle = BehaviorHandle::Null();
					continue;
				}
				record->instance->SetSerializedFields(entry.serializedFields);
			}

			// アクセスされたフラグを立てる
			record->seen = true;

			// 対象エンティティが階層内でアクティブか
			bool shouldBeEnabled = entry.enabled && IsEntityActiveInHierarchy(world, entity);

			//============================================================================
			//	Awakeを実行
			//	最初にアクセスされたときに1回だけ呼ばれる
			//============================================================================
			if (!record->awakeCalled) {

				record->instance->Awake(world, context, entity);
				record->awakeCalled = true;
			}

			//============================================================================
			//	OnEnable/OnDisableを実行
			//	フラグの状態に応じて呼ばれる
			//============================================================================
			if (shouldBeEnabled && !record->enabled) {

				record->instance->OnEnable(world, context, entity);
				record->enabled = true;
			}
			if (!shouldBeEnabled && record->enabled) {

				record->instance->OnDisable(world, context, entity);
				record->enabled = false;
			}

			//============================================================================
			//	Startを実行
			//	初期化が完了して、最初に有効になったときに1回だけ呼ばれる
			//============================================================================
			if (record->enabled && record->awakeCalled && !record->startCalled) {

				record->instance->Start(world, context, entity);
				record->startCalled = true;
			}
		}
		});
	// 更新時、参照されなくなったビヘイビアをワールドから破棄する
	if (doSweep) {

		runtime_.SweepUnseen(world, context);
	}
}
