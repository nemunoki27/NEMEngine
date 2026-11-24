using NEMEngine;

namespace NEMTests.Scripts;

[ScriptTypeID("78cd432f-0bd2-47ec-9b0f-d4221384e622")]
public sealed class ClothAPIProbe : MonoBehaviour {

	[SerializeField, SerializedFieldID("13663eaa-f905-4328-9315-cfcc22b5c1c3")] private bool passed;
	[SerializeField, SerializedFieldID("2ca2b4aa-6e68-46b6-88d8-203fbffdda05")] private string error = string.Empty;

	private void Start() {
		try {
			var cloth = gameObject.AddComponent<Cloth>();
			if (cloth.CollisionMask != uint.MaxValue) throw new InvalidOperationException("衝突レイヤーの初期値が一致しません");
			cloth.Enabled = false;
			cloth.PinMode = ClothPinMode.Fasteners;
			cloth.Edge = ClothEdge.Top;
			cloth.Fasteners = 7;
			cloth.CollisionMask = 0x80000004u;
			cloth.WindDirection = new Vector3(0.25f, 0, 1);
			cloth.WindStrength = 4;
			cloth.WindEnabled = false;
			cloth.ReceiveSceneWind = true;
			cloth.WindCollisionMask = 2;
			cloth.BendCompliance = 0.002f;
			cloth.Reset();
			if (cloth.WindEnabled || !cloth.ReceiveSceneWind || cloth.WindCollisionMask != 2 ||
				cloth.Enabled || cloth.PinMode != ClothPinMode.Fasteners || cloth.Edge != ClothEdge.Top ||
				cloth.Fasteners != 7 || cloth.WindDirection != new Vector3(0.25f, 0, 1) || cloth.WindStrength != 4 ||
				cloth.BendCompliance != 0.002f || cloth.CollisionMask != 0x80000004u)
				throw new InvalidOperationException("Clothの設定または衝突レイヤーが一致しません");
			cloth.CollisionMask = 0;
			passed = cloth.CollisionMask == 0;
		} catch (Exception exception) { error = exception.ToString(); }
	}
}
