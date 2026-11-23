using System.Reflection;
using NEMEngine;

public static class ProductComponents {
    private static Type[] types = Array.Empty<Type>();
    private static GameObject? owner;
    private static AnimationClip? motion;
    private static TimelineAsset? sequence;
    private static AudioClip? tone;
    private static int index;
    private static int phase;
    private static int properties;
    private static bool started;
    private static int settleFrames;
    private static object? current;

    private static void Exercise(object component) {
        switch (component) {
            case AnimationClipPlayer player:
                player.Play(motion!);
                break;
            case TimelinePlayer timeline:
                timeline.Timeline = sequence;
                timeline.SetBinding("Mover", owner);
                timeline.Play();
                break;
            case AudioSource audio:
                audio.Clip = tone;
                audio.Loop = true;
                audio.Volume = 0.05f;
                audio.Play();
                break;
            case NEMEngine.Timer timer:
                timer.Mode = TimerMode.Frames;
                timer.TargetFrames = 2;
                timer.Play();
                break;
            case SimpleAnimation animation:
                animation.SetSettings(new SimpleAnimationSettings<float> { Start = 2, End = 8, Duration = 1, Speed = 0 });
                animation.Seek(0.5f);
                break;
            case BeamRenderer beam:
                beam.Mode = BeamMode.Lightning;
                beam.AddWaypoint(BeamEndpoint.Local(new Vector3(1, 2, 3)));
                if (beam.WaypointCount == 0 || beam.GetWaypoint(beam.WaypointCount - 1).Position.y != 2)
                    throw new InvalidOperationException("Beam waypoint");
                beam.RemoveWaypoint(beam.WaypointCount - 1);
                break;
            case TrailRenderer trail:
                trail.Clear();
                trail.AddPosition(Vector3.zero);
                trail.AddPosition(new Vector3(3, 0, 0));
                if (trail.PositionCount < 2) throw new InvalidOperationException("Trail history");
                trail.Clear();
                break;
            case Spline spline:
                spline.pointCount = 2;
                spline.SetPoint(1, new SplineControlPoint(new Vector3(7, 0, 0), Vector3.zero, Vector3.zero));
                if (spline.GetPoint(1).position.x != 7) throw new InvalidOperationException("Spline point");
                break;
            case Transform transform:
                transform.localPosition = new Vector3(1, 2, 3);
                if (transform.localPosition.z != 3) throw new InvalidOperationException("Transform position");
                break;
            case CollisionComponent collision:
                collision.Shape.Type = ColliderShapeType.Sphere3D;
                collision.Shape.Enabled = true;
                collision.Shape.Radius = 2;
                break;
        }
    }

    public static void Begin(AnimationClip? clip, TimelineAsset? timeline, AudioClip? sound) {
        if (started || Environment.GetEnvironmentVariable("NEM_PRODUCT_COMPONENT_TEST") != "1") return;
        started = true;
        if (clip == null || !clip.exists || timeline == null || !timeline.exists || sound == null || !sound.exists)
            throw new InvalidOperationException("Unresolved playback asset reference");
        motion = clip;
        sequence = timeline;
        tone = sound;
        types = typeof(Component).Assembly.GetExportedTypes()
            .Where(type => !type.IsAbstract && type.GetInterfaces().Any(contract =>
                contract.IsGenericType && contract.GetGenericTypeDefinition() == typeof(IComponentRef<>)))
            .OrderBy(type => type.Name).ToArray();
        if (types.Length == 0) throw new InvalidOperationException("No product component bindings.");
    }

    private static object? Invoke(string method, Type type) {
        return typeof(GameObject).GetMethod(method, Type.EmptyTypes)!
            .MakeGenericMethod(type).Invoke(owner, null);
    }

    public static bool Tick() {
        if (!ProductInteractions.Tick()) return false;
        if (index == types.Length) return true;
        Type type = types[index];
        switch (phase) {
            case 0:
                owner = new GameObject("Check " + type.Name);
                Invoke(nameof(GameObject.AddComponent), type);
                break;
            case 1:
                object component = Invoke(nameof(GameObject.GetComponent), type)
                    ?? throw new InvalidOperationException("Missing " + type.Name);
                properties = 0;
                foreach (var property in type.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly)) {
                    if (!property.CanRead || property.GetIndexParameters().Length != 0) continue;
                    try { property.GetValue(component); }
                    catch (Exception ex) { throw new InvalidOperationException(type.Name + "." + property.Name, ex); }
                    ++properties;
                }
                current = component;
                Exercise(component);
                settleFrames = 0;
                break;
            case 2:
                if (++settleFrames < 8) return false;
                if (current is AnimationClipPlayer && Math.Abs(owner!.transform.localPosition.x - 13) > 0.001f)
                    throw new InvalidOperationException("Packed animation clip did not move the owner");
                if (current is TimelinePlayer timeline && (timeline.Duration == 0 || Math.Abs(owner!.transform.localPosition.x - 21) > 0.001f)) {
                    if (settleFrames < 300) return false;
                    throw new InvalidOperationException("Packed timeline did not evaluate the bound Transform");
                }
                if (current is AudioSource audio) {
                    if (!audio.IsPlaying) {
                        if (settleFrames < 180) return false;
                        throw new InvalidOperationException("Packed PCM did not play");
                    }
                    audio.Stop();
                }
                if (current is NEMEngine.Timer timer && !timer.IsReached)
                    throw new InvalidOperationException("Timer did not reach target");
                if (current is SimpleAnimation animation && Math.Abs(animation.GetValue<float>() - 5) > 0.001f)
                    throw new InvalidOperationException("SimpleAnimation seek");
                if (current is CollisionComponent && (!Physics.Raycast(new Vector3(0, 0, -10),
                    Vector3.forward, out RaycastHit hit, 20) || hit.gameObject != owner))
                    throw new InvalidOperationException("Sphere raycast");
                current = null;
                // Transform は GameObject の生存期間を通して保持される。
                if (type != typeof(Transform)) Invoke(nameof(GameObject.RemoveComponent), type);
                break;
            case 3:
                if (type != typeof(Transform) && Invoke(nameof(GameObject.GetComponent), type) != null)
                    throw new InvalidOperationException("Component still attached: " + type.Name);
                owner!.Destroy();
                break;
            case 4:
                if (owner != null) throw new InvalidOperationException("Owner still alive: " + type.Name);
                var report = Environment.GetEnvironmentVariable("NEM_PRODUCT_FLOW_REPORT");
                if (report != null) File.AppendAllText(report, $"component {type.Name} getters={properties}\n");
                owner = null;
                ++index;
                break;
        }
        phase = (phase + 1) % 5;
        return index == types.Length;
    }
}
