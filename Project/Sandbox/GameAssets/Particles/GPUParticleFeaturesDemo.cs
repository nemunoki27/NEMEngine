using NEMEngine;

namespace SandboxScripts;

public enum GPUParticleDemoMode {

	メッシュ頂点,
	メッシュ表面,
	カールと共有力場,
	曲線と周回,
	形状衝突,
	カメラ深度衝突,
	子粒子と塗装,
	深度フェードと受光,
	任意データの色,
	画像から発生,
}

// 空Entityへ付けてPlayし、F8でGPU粒子の確認項目を切り替える
[ScriptTypeID("48b26b7a-8f43-4d6b-a03b-b1c4bb93d062")]
public sealed class GPUParticleFeaturesDemo : MonoBehaviour {

	[SerializeField, Label("確認する項目")]
	private GPUParticleDemoMode mode = GPUParticleDemoMode.メッシュ表面;
	[SerializeField, Label("確認用カメラを作る")]
	private bool createCamera = true;
	[SerializeField, Label("発生元メッシュ・未指定は球")]
	private Mesh? sourceMesh;
	[SerializeField, Label("発生元Renderer・指定時は移動やスキニングを追従")]
	private MeshRenderer? sourceRenderer;
	[SerializeField, Label("発生元画像・未指定は既存の方向画像")]
	private Texture? sourceTexture;
	[SerializeField, Label("粒子の画像・省略可能")]
	private Texture? particleTexture;
	[SerializeField, Label("粒子マテリアル・未指定は標準")]
	private Material? particleMaterial;
	[SerializeField, Label("任意データ用マテリアル・未指定は確認用Graph")]
	private Material? customDataMaterial;
	[SerializeField, Label("命中Decalマテリアル・未指定は既存の傷")]
	private Material? decalMaterial;
	[SerializeField, Label("FGA・未指定は渦のプリセット")]
	private DefaultAsset? vectorField;
	[SerializeField, Label("最大粒子数"), Min(128)]
	private int maxParticles = 4096;
	[SerializeField, Label("1秒あたりの発生数"), Min(1)]
	private float particlesPerSecond = 240;
	[SerializeField, Label("障害物を動かす")]
	private bool moveObstacle = true;

	private GPUParticleDemoStage? stage;
	private GPUParticleDemoMode appliedMode;
	private bool prepared;
	private int createdFrame;

	private void Start() {

		stage = new GPUParticleDemoStage(transform, createCamera);
		createdFrame = Time.frameCount;
		PrintControls();
	}

	private void Update() {

		if (stage == null || Time.frameCount == createdFrame) return;
		// 生成予約の適用後に保存参照とParticle設定を取得する
		if (!prepared) {
			stage.Prepare();
			ConfigureChild();
			prepared = true;
			ApplyMode();
		}
		if (Input.GetKeyDown(KeyCode.F8)) {
			int direction = Input.GetKey(KeyCode.LeftShift) ? -1 : 1;
			mode = (GPUParticleDemoMode)(((int)mode + direction + 10) % 10);
		}
		if (mode != appliedMode || Input.GetKeyDown(KeyCode.F9)) ApplyMode();
		if (Input.GetKeyDown(KeyCode.F10)) stage.Receiver.Clear();
		if (Input.GetKeyDown(KeyCode.F11)) PrintControls();
		stage.Animate(moveObstacle, mode == GPUParticleDemoMode.曲線と周回);
	}

	// 未保存の確認用Entityは実行Worldだけで所有する
	private void OnDestroy() {

		stage?.Destroy();
		stage = null;
	}

	// 共通の表示と発生値を作り、選んだ項目だけを組み合わせる
	private void ApplyMode() {

		var current = stage!;
		current.Particles.Stop(false, ParticleSystemStopBehavior.StopEmittingAndClear);
		current.Child.Clear(false);
		current.Receiver.Clear();
		current.SetFields(mode == GPUParticleDemoMode.カールと共有力場, vectorField);
		var settings = MakeSettings(current.Particles, 4, 0.075f);
		var phase = settings.phases[0];
		current.Emitter.position = current.Origin + new Vector3(0, 2.4f, 0);
		switch (mode) {
		case GPUParticleDemoMode.メッシュ頂点:
		case GPUParticleDemoMode.メッシュ表面:
			settings.source.kind = mode == GPUParticleDemoMode.メッシュ頂点 ? ParticleSourceKind.MeshVertex : ParticleSourceKind.MeshSurface;
			settings.source.mesh = sourceMesh?.assetID ?? AssetGUID.Parse("4e454d41535345544195d8a0c7815abd");
			settings.source.renderer = ParticleEntityReference.FromGameObject(sourceRenderer?.gameObject);
			settings.source.sequential = mode == GPUParticleDemoMode.メッシュ頂点;
			settings.emitter.speed = 0.8f;
			break;
		case GPUParticleDemoMode.カールと共有力場:
			var curl = phase.AddCurlNoise();
			curl.strength = 2.5f;
			curl.frequency = 0.8f;
			curl.scrollSpeed = new Vector3(0, 0.3f, 0);
			phase.AddForceField().layerMask = GPUParticleDemoStage.LayerMask;
			phase.AddVectorField().layerMask = GPUParticleDemoStage.LayerMask;
			break;
		case GPUParticleDemoMode.曲線と周回:
			settings.shape = PrimitiveType.Cube;
			settings.billboard = false;
			var scale = phase.AddScaleOverLifetime();
			scale.startScale = scale.endScale = new Vector3(0.7f, 0.7f, 2.5f);
			var path = phase.AddSplineMovement();
			path.spline = ParticleEntityReference.FromGameObject(current.Curve.gameObject);
			path.speed = 4;
			path.orbitRadius = 0.25f;
			path.orbitTurnsPerMeter = 0.6f;
			break;
		case GPUParticleDemoMode.形状衝突:
		case GPUParticleDemoMode.カメラ深度衝突:
		case GPUParticleDemoMode.子粒子と塗装:
			ConfigureRain(settings);
			var collision = phase.AddCollision();
			collision.enabled = true;
			collision.layerMask = GPUParticleDemoStage.LayerMask;
			collision.radius = 0.06f;
			collision.bounce = 0.5f;
			collision.friction = 0.15f;
			if (mode == GPUParticleDemoMode.カメラ深度衝突) {
				collision.sceneColliders = false;
				collision.depth = true;
				collision.depthCamera = ParticleEntityReference.FromGameObject(current.CameraObject);
			} else if (mode == GPUParticleDemoMode.子粒子と塗装) ConfigureFeedback(settings);
			break;
		case GPUParticleDemoMode.深度フェードと受光:
			ConfigureRain(settings);
			settings.blendMode = BlendMode.Normal;
			settings.surface.softParticles = settings.surface.lit = true;
			settings.surface.softDistance = 0.5f;
			var size = phase.AddSizeOverLifetime();
			size.startScale = 0.5f;
			size.endScale = 0.2f;
			break;
		case GPUParticleDemoMode.任意データの色:
			settings.material = customDataMaterial?.assetID ?? AssetGUID.Parse("a7c445614a8d41c8a4a2cb70f070a002");
			var data = phase.AddCustomData().customData1;
			data.mode = ParticleParameterMode.OverLifetime;
			data.start = new Vector4(0.1f, 0.7f, 1, 1);
			data.end = new Vector4(1, 0.15f, 0.05f, 1);
			var white = phase.AddColorOverLifetime();
			white.startColor = white.endColor = Color4.white;
			break;
		case GPUParticleDemoMode.画像から発生:
			settings.source.kind = ParticleSourceKind.Texture;
			settings.source.texture = sourceTexture?.assetID ?? current.DefaultTexture;
			settings.source.textureSize = new Vector2(4, 3);
			settings.source.inheritColor = true;
			settings.emitter.speed = 0.15f;
			break;
		}
		current.Particles.SetSettings(settings);
		current.Particles.Enabled = true;
		current.Particles.Play(false);
		appliedMode = mode;
		Debug.Log($"GPU粒子の確認: {mode}。F8=次へ、Shift+F8=前へ、F9=設定を反映して再開、F10=塗装消去");
	}

	// 色と寿命を保ち、設定編集には公開された型だけを使う
	private ParticleSystemSettings MakeSettings(ParticleSystem particles, float lifetime, float size) {

		var settings = particles.GetSettings();
		settings.looping = true;
		settings.material = particleMaterial?.assetID ?? AssetGUID.None;
		settings.shape = PrimitiveType.Plane;
		settings.billboard = true;
		settings.blendMode = BlendMode.Add;
		settings.source.kind = ParticleSourceKind.Analytic;
		settings.source.inheritColor = false;
		settings.surface.softParticles = settings.surface.lit = false;
		settings.feedback.paint.enabled = settings.feedback.decal.enabled = false;
		while (settings.feedback.events.Count > 0) settings.feedback.RemoveEventAt(0);
		settings.emitter.maxParticles = (uint)Math.Clamp(maxParticles, 128, 65536);
		settings.emitter.emitInterval = 1.0f / Math.Clamp(particlesPerSecond / 4.0f, 1, 1000);
		settings.emitter.emitCount = 4;
		settings.emitter.shape = ParticleEmitterShape.Sphere;
		settings.emitter.speed = 1.2f;
		settings.emitter.SetOffset(Vector3.zero);
		var phase = settings.phases[0];
		phase.ClearModules();
		phase.lifetime = lifetime;
		phase.baseColorTexture = particleTexture?.assetID ?? AssetGUID.None;
		var scale = phase.AddSizeOverLifetime();
		scale.startScale = size;
		scale.endScale = size * 0.4f;
		var color = phase.AddColorOverLifetime();
		color.startColor = new Color4(0.15f, 0.65f, 1, 1);
		color.endColor = new Color4(1, 0.25f, 0.08f, 0);
		return settings;
	}

	// 表面への命中位置から少数の子粒子と塗装を発生させる
	private void ConfigureFeedback(ParticleSystemSettings settings) {

		var collision = settings.phases[0].GetCollision()!;
		collision.response = ParticleCollisionResponse.Kill;
		var route = settings.feedback.AddEvent(ParticleEventKind.Collision, ParticleEntityReference.FromGameObject(stage!.Child.gameObject));
		route.count = 6;
		settings.feedback.paint.enabled = true;
		settings.feedback.paint.radius = 0.2f;
		settings.feedback.paint.color = new Color4(0.1f, 0.5f, 1, 1);
		settings.feedback.decal.enabled = true;
		settings.feedback.decal.material = decalMaterial?.assetID ?? AssetGUID.Parse("d3ca1000000000000000000000000001");
		settings.feedback.decal.lifetime = 4;
		settings.feedback.decal.capacity = 256;
	}

	// 子Systemは通常放出を止め、GPUイベントからだけ生成する
	private void ConfigureChild() {

		var settings = MakeSettings(stage!.Child, 0.7f, 0.11f);
		settings.emitter.emitCount = 0;
		settings.emitter.speed = 2;
		settings.phases[0].AddGravity().gravity = new Vector3(0, -4, 0);
		stage.Child.SetSettings(settings);
		stage.Child.Enabled = true;
		stage.Child.Play(false);
	}

	// 形状と深度の衝突を同じ落下条件で比較する
	private void ConfigureRain(ParticleSystemSettings settings) {

		stage!.Emitter.position = stage.Origin + new Vector3(0, 4, 0);
		settings.emitter.shape = ParticleEmitterShape.Point;
		settings.emitter.pointDirection = Vector3.down;
		settings.emitter.speed = 1;
		settings.emitter.SetOffsetRange(new Vector3(-3, 0, -2), new Vector3(3, 0, 2));
		settings.phases[0].AddGravity().gravity = new Vector3(0, -6, 0);
	}

	// GameViewへフォーカスして操作する
	private void PrintControls() {

		Debug.Log("GPU粒子の確認: F8で項目切替、Shift+F8で逆順、F9で再開、F10で塗装消去、F11で操作表示。Inspectorの確認項目も変更できます");
	}
}
