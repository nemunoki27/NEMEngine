using NEMEngine;

namespace SandboxScripts;

// 既存のSceneを触らず確認用の床、形状、曲線、Cameraを所有する
internal sealed class GPUParticleDemoStage {

	internal const uint LayerMask = 1u << 20;
	internal Vector3 Origin { get; }
	internal GameObject? CameraObject { get; }
	internal ParticleSystem Particles { get; }
	internal ParticleSystem Child { get; }
	internal Transform Emitter => Particles.transform;
	internal Spline Curve { get; }
	internal ParticlePaintReceiver Receiver { get; }
	internal AssetGUID DefaultTexture => AssetGUID.Parse("4e454d4153534554b1295060516f76cf");
	private readonly GameObject root;
	private readonly GameObject movingBox;
	private readonly ForceField force;
	private readonly VectorField vectors;

	internal GPUParticleDemoStage(Transform owner, bool createCamera) {

		Origin = owner.position;
		root = new GameObject("GPU粒子・実行時の確認用");
		root.transform.position = Origin;
		root.transform.SetParent(owner, true);
		Particles = AddParticles("GPU粒子・確認するSystem");
		Child = AddParticles("GPU粒子・命中時の子System");
		var floor = AddBox("GPU粒子・塗装する床", new Vector3(0, -0.1f, 0), new Vector3(9, 0.2f, 7));
		Receiver = floor.AddComponent<ParticlePaintReceiver>();
		Receiver.projection = ParticlePaintProjection.PlanarXZ;
		Receiver.planarSize = new Vector2(9, 7);
		Receiver.resolution = 512;
		movingBox = AddBox("GPU粒子・動く箱", new Vector3(1.7f, 0.8f, 0), new Vector3(1.3f, 1.5f, 1.3f));
		var ball = AddObject("GPU粒子・球", new Vector3(-1.8f, 0.8f, 0));
		AddVisual(ball, PrimitiveType.Sphere).Sphere.Radius = 0.8f;
		AddCollision(ball, ColliderShapeType.Sphere3D).Shape.Radius = 0.8f;
		var capsule = AddObject("GPU粒子・カプセル", new Vector3(0, 1.1f, 1.6f));
		var shape = AddCollision(capsule, ColliderShapeType.Capsule3D).Shape;
		shape.Radius = 0.4f;
		shape.CapsuleHeight = 2.2f;
		var body = AddVisual(capsule, PrimitiveType.Cylinder);
		body.Cylinder.Height = 1.4f;
		body.Cylinder.TopRadius = body.Cylinder.CenterRadius = body.Cylinder.BottomRadius = 0.4f;
		foreach (float y in new[] { 0.4f, 1.8f })
			AddVisual(AddObject("GPU粒子・カプセルの端", new Vector3(0, y, 1.6f)), PrimitiveType.Sphere).Sphere.Radius = 0.4f;
		var fields = AddObject("GPU粒子・共有する場", new Vector3(0, 1.5f, 0));
		force = fields.AddComponent<ForceField>();
		force.type = ParticleForceFieldType.Vortex;
		force.strength = 5;
		force.range = 6;
		force.layerMask = LayerMask;
		force.enabled = false;
		vectors = fields.AddComponent<VectorField>();
		vectors.size = new Vector3(8, 8, 8);
		vectors.intensity = 1.5f;
		vectors.tightness = 0.02f;
		vectors.layerMask = LayerMask;
		vectors.enabled = false;
		Curve = AddObject("GPU粒子・編集できる曲線", Vector3.zero).AddComponent<Spline>();
		var light = AddObject("GPU粒子・確認用の光", Vector3.zero).AddComponent<DirectionalLight>();
		light.Direction = new Vector3(0.4f, -1, 0.3f);
		light.Intensity = 2;
		if (createCamera) {
			CameraObject = AddObject("GPU粒子・確認用Camera", new Vector3(0, 4.5f, -11));
			CameraObject.transform.rotation = Quaternion.LookRotation(Origin + new Vector3(0, 1.5f, 0) - CameraObject.transform.position);
			var camera = CameraObject.AddComponent<PerspectiveCamera>();
			camera.IsMain = true;
			camera.Priority = 1000;
			camera.NearClip = 0.05f;
			camera.FarClip = 100;
		}
	}

	// Worldへの追加後に曲線の外部Storageを編集する
	internal void Prepare() {

		Curve.pointCount = 4;
		Curve.closed = true;
		const float tangent = 1.10457f;
		Curve.SetPoint(0, new(new Vector3(-2, 1.5f, 0), new Vector3(0, 0, tangent), new Vector3(0, 0, -tangent)));
		Curve.SetPoint(1, new(new Vector3(0, 2.5f, -2), new Vector3(-tangent, 0, 0), new Vector3(tangent, 0, 0)));
		Curve.SetPoint(2, new(new Vector3(2, 1.5f, 0), new Vector3(0, 0, -tangent), new Vector3(0, 0, tangent)));
		Curve.SetPoint(3, new(new Vector3(0, 0.7f, 2), new Vector3(tangent, 0, 0), new Vector3(-tangent, 0, 0)));
	}

	// 回転と並進するColliderの接触速度も確認する
	internal void Animate(bool move, bool drawSpline) {

		float time = move ? Time.time : 0;
		movingBox.transform.position = Origin + new Vector3(1.7f + Mathf.Sin(time) * 0.6f, 0.8f, 0);
		movingBox.transform.rotation = Quaternion.Euler(0, time * 25, 15);
		if (!drawSpline) return;
		float length = Curve.EvaluateDistance(float.MaxValue, false).distance;
		Vector3 previous = Curve.EvaluateDistance(0, false).position;
		for (int i = 1; i <= 48; ++i) {
			Vector3 next = Curve.EvaluateDistance(length * i / 48, false).position;
			LineDraw.DrawLine(previous, next, new Color4(1, 0.7f, 0.1f, 1), 0.015f);
			previous = next;
		}
	}

	// 同じSceneの複数Systemが共有する場を有効にする
	internal void SetFields(bool enabled, DefaultAsset? field) {

		force.enabled = vectors.enabled = enabled;
		vectors.preset = field ? ParticleVectorFieldPreset.Asset : ParticleVectorFieldPreset.Vortex;
		vectors.field = field;
	}

	// Script単体の削除でも確認用階層を残さない
	internal void Destroy() {

		if (root) root.Destroy();
	}

	// 通常放出前に設定できる停止状態で作る
	private ParticleSystem AddParticles(string name) {

		var particles = AddObject(name, Vector3.zero).AddComponent<ParticleSystem>();
		particles.Enabled = false;
		particles.PlayOnAwake = false;
		particles.SimulationTarget = ParticleSimulationTarget.GPU;
		particles.useAutoRandomSeed = false;
		particles.randomSeed = 12345;
		return particles;
	}

	// 追加するEntityを確認用階層だけへまとめる
	private GameObject AddObject(string name, Vector3 offset) {

		var value = new GameObject(name);
		value.transform.position = Origin + offset;
		value.transform.SetParent(root.transform, true);
		return value;
	}

	// 見た目と同じ大きさの箱を作る
	private GameObject AddBox(string name, Vector3 position, Vector3 size) {

		var value = AddObject(name, position);
		AddVisual(value, PrimitiveType.Cube).Cube.Size = size;
		AddCollision(value, ColliderShapeType.OBB3D).Shape.HalfExtents3D = size * 0.5f;
		return value;
	}

	// 標準の不透明描画で深度と塗装を受け取る
	private PrimitiveRenderer AddVisual(GameObject owner, PrimitiveType type) {

		var renderer = owner.AddComponent<PrimitiveRenderer>();
		renderer.Type = type;
		renderer.Queue = RenderPhase.Opaque;
		renderer.receiveDecals = true;
		return renderer;
	}

	// 既存SceneのColliderと確認用Colliderを分ける
	private CollisionComponent AddCollision(GameObject owner, ColliderShapeType type) {

		var collision = owner.AddComponent<CollisionComponent>();
		collision.IsStatic = true;
		collision.TypeMask = LayerMask;
		collision.Shape.Type = type;
		return collision;
	}
}
