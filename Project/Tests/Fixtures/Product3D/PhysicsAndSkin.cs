using NEMEngine;

public sealed class PhysicsAndSkin : MonoBehaviour {
    private readonly string report = Environment.GetEnvironmentVariable("NEM_PRODUCT_3D_REPORT")!;
    private GameObject robot = null!, moving = null!, wall = null!, pinned = null!, hinged = null!, anchor = null!;
    private bool contact, jointsReleased, running, walking;
    private GameObject marker = null!;
    private Vector3? previousJoint;
    private float jointTravel;
    private int frame;
    private float runningLength, walkingLength;

    private void Log(string line) => File.AppendAllText(report, line + Environment.NewLine);

    private void Start() {
        robot = GameObject.Find("RobotExpressive")!;
        marker = new GameObject("Palm marker");
        marker.AddComponent<JointAttachment>();
        moving = new GameObject("Moving");
        wall = new GameObject("Wall");
        anchor = new GameObject("Anchor");
        pinned = new GameObject("Pinned");
        hinged = new GameObject("Hinged");
        foreach (var body in new[] { moving, pinned, hinged }) body.AddComponent<Rigidbody>();
        moving.AddComponent<CollisionComponent>();
        wall.AddComponent<CollisionComponent>();
        pinned.AddComponent<FixedJoint>();
        hinged.AddComponent<HingeJoint>();
    }

    private static void Sphere(GameObject item, bool stationary) {
        var collider = item.GetComponent<CollisionComponent>()!;
        collider.IsStatic = stationary;
        collider.Shape.Type = ColliderShapeType.Sphere3D;
        collider.Shape.Radius = 0.5f;
        collider.Shape.Enabled = true;
    }

    private void Configure() {
        var attachment = marker.GetComponent<JointAttachment>()!;
        attachment.SkinnedEntityLocalFileID = robot;
        attachment.JointName = "Palm2.L";
        moving.transform.localPosition = Vector3.zero;
        wall.transform.localPosition = new Vector3(2.5f, 0, 0);
        foreach (var item in new[] { anchor, pinned, hinged })
            item.transform.localPosition = new Vector3(10, 0, 0);
        Sphere(moving, false);
        Sphere(wall, true);
        var joint = pinned.GetComponent<FixedJoint>()!;
        joint.ConnectedBody = anchor;
        joint.AutoConfigureConnectedAnchor = false;
        var hinge = hinged.GetComponent<HingeJoint>()!;
        hinge.ConnectedBody = anchor;
        hinge.AutoConfigureConnectedAnchor = false;
        foreach (var item in new[] { moving, pinned, hinged }) {
            var body = item.GetComponent<Rigidbody>()!;
            body.BodyType = RigidbodyType.Dynamic;
            body.UseGravity = false;
            body.Mass = 2;
            body.AddForce(new Vector3(8, 0, 0), ForceMode.Impulse);
            if (Math.Abs(body.LinearVelocity.x - 4) > 0.001f) throw new Exception("Unexpected impulse velocity");
        }
    }

    private void Update() {
        try {
            ++frame;
            if (frame == 3) Configure();
            if (frame < 5) return;
            var skin = robot.GetComponent<SkinnedAnimation>()!;
            if (runningLength == 0) {
                runningLength = skin.GetDuration("Running");
                walkingLength = skin.GetDuration("Walking");
                if (0 < runningLength && 0 < walkingLength) skin.Play("Running");
                else runningLength = 0;
            }
            if (skin.CurrentState == "Running" && skin.IsPlaying && !skin.InTransition) {
                Vector3 position = marker.transform.position;
                if (previousJoint.HasValue) jointTravel += (position - previousJoint.Value).magnitude;
                previousJoint = position;
            }
            if (!running && 0.01f < jointTravel && skin.CurrentState == "Running" && skin.IsPlaying && 0.12f < skin.CurrentTime) {
                running = true;
                Log($"Running t={skin.CurrentTime:F3} length={runningLength:F3} palm travel={jointTravel:F3}");
                skin.Play("Walking");
            }
            if (!walking && running && skin.CurrentState == "Walking" && skin.IsPlaying && 0.16f < skin.CurrentTime && !skin.InTransition) {
                walking = true;
                Log($"Walking t={skin.CurrentTime:F3} length={walkingLength:F3}");
            }
            if (!contact && 1 < moving.transform.localPosition.x && wall.GetComponent<CollisionComponent>()!.IsColliding) {
                if (!Physics.Raycast(new Vector3(4, 0, 0), new Vector3(-1,0,0), out var hit, 3) || hit.gameObject != wall)
                    throw new Exception("Raycast missed the wall");
                contact = true;
                Log($"sphere contact x={moving.transform.localPosition.x:F3}, raycast=Wall");
            }
            if (!jointsReleased && contact) {
                if (0.15f < Math.Abs(10 - pinned.transform.localPosition.x) || 0.15f < Math.Abs(10 - hinged.transform.localPosition.x))
                    throw new Exception($"Joint displacement fixed={pinned.transform.localPosition.x} hinge={hinged.transform.localPosition.x}");
                pinned.GetComponent<FixedJoint>()!.Enabled = false;
                hinged.GetComponent<HingeJoint>()!.Enabled = false;
                jointsReleased = true;
                Log("FixedJoint and HingeJoint held against impulse");
            }
            if (walking && jointsReleased && 10.5f < pinned.transform.localPosition.x && 10.5f < hinged.transform.localPosition.x) {
                Log($"released x={pinned.transform.localPosition.x:F3}, {hinged.transform.localPosition.x:F3}");
                Log("PASS");
                Application.Quit();
            }
            if (1200 < frame) throw new Exception($"Timeout skin={skin.CurrentState} t={skin.CurrentTime} duration={runningLength}, contact={contact}, x={moving.transform.localPosition.x}, j={pinned.transform.localPosition.x}/{hinged.transform.localPosition.x}");
        } catch (Exception error) {
            Log("FAIL " + error);
            Application.Quit();
        }
    }
}
