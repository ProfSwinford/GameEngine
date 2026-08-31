// =============================================================================
//  The sandbox: the standalone game runtime. No IDE, no ImGui.
//
//  This is what the Week 10 gate is built in, and what proves the engine can
//  ship without its tools attached. It links `engine` and cannot reach SDL -
//  Week 3 made that link PRIVATE.
//
//  Week 1's version was a naive poll/clear/present loop with a hardcoded
//  window size. Both are gone: the loop is GameClock's fixed timestep and the
//  window size comes from config/engine.json.
//
//  Week 1 stretch goal 3 parsed argc/argv crudely for the window size. Week 8
//  deleted that, as it promised it would - and the point of having done it
//  once badly is that the difference is obvious. The flags that remain select
//  which of the semester's measurements to run rather than configuring the
//  engine, which is a different job.
// =============================================================================

#include "CollectorGame.h"
#include "LayoutBench.h"
#include "PlatformBench.h"

#include <engine/Engine.h>

#include <cstdio>
#include <filesystem>
#include <format>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void PrintUsage() {
    std::printf(
        "sandbox - the Engine2D standalone runtime\n"
        "\n"
        "  (no flags)              run the startup scene from config/engine.json\n"
        "  --game                  run the Week 10 gate exercise (Spec A, Collector)\n"
        "  --autoplay              --game driven by the InputMap AI hook, so the whole\n"
        "                          round can be played without a human at the keyboard\n"
        "  --scene <virtual/path>  load a different scene\n"
        "  --frames <N>            run N frames then exit cleanly (for automation)\n"
        "  --config <virtual/path> use a different config file\n"
        "\n"
        "  --sizeof-audit          Week 4: sizeof/alignof of the real engine structs\n"
        "  --layout-bench [N]      Week 4: AoS vs SoA over N elements (default 100000)\n"
        "  --os-measure            Week 5: thread create, context switch, page fault\n"
        "  --random-check [seed]   Week 6: print 20 values from a fixed seed\n"
        "  --motion-check          Week 6: proves the three-deep hierarchy really orbits\n"
        "  --save-check [scene]    scene save/load round trip is a fixed point\n"
        "  --m3-check              Week 9: refcount table, unload to zero, stale handle\n"
        "  --collision-check       Week 10: enter/stay/exit and the layer-mask CVar\n"
        "  --playmode-check        the editor's Play then Stop restores the scene\n"
        "  --script-check          scripts attach, run and round trip - compiled or not\n"
        "  --stress [frames]       Week 10: the 1000-frame spawn/destroy stress run\n"
        "  --fail-subsystem <name> Week 7: force one subsystem's Init to return false\n"
        "\n"
        "  --help                  this text\n");
}

// WEEK 6 - the cross-machine determinism check. Prints the first twenty values
// from a fixed seed; run on two machines and diff. Identical output is the
// Milestone 1 verification, and it is only identical because Random.h writes
// out its own generator instead of using a std:: distribution.
int RunRandomCheck(eng::u64 seed) {
    eng::Random random(seed);
    std::printf("seed %llu\n", static_cast<unsigned long long>(seed));
    for (int i = 0; i < 20; ++i) {
        // Printed as raw u32 and as a hex float bit pattern: a decimal float
        // could differ in the last digit purely because of printf rounding,
        // and the whole point is a byte-identical diff.
        const eng::u32 value = random.NextU32();
        std::printf("%2d  u32=%10u  int[1,6]=%d\n", i, value,
                    eng::Random(seed + static_cast<eng::u64>(i)).NextInt(1, 6));
    }
    return 0;
}

// WEEK 10 - the 1000-frame stress run. A scripted scene where A spawns B and
// destroys C in the same frame, checked for crashes, leaked components, and
// STABLE allocator numbers. A slow climb is a leak that has not finished yet.
int RunStress(int frames) {
    eng::Engine::Options options;
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    eng::Scene& scene = eng::Engine::Get().GetScene();

    const eng::usize entitiesAtStart = scene.EntityCount();
    const eng::usize bytesAtStart    = eng::MemorySystem::TotalBytesUsed();
    const eng::u64   refsAtStart     = eng::ResourceManager::TotalRefCount();

    ENGINE_LOG_INFO(eng::Channels::kGame,
                    "stress: {} frames, spawn one and destroy one per frame", frames);
    ENGINE_LOG_INFO(eng::Channels::kGame, "frame 1: {} entities, {} allocator bytes",
                    entitiesAtStart, bytesAtStart);

    eng::u64 spawnCounter = 0;

    for (int frame = 0; frame < frames && eng::Engine::Get().BeginFrame(); ++frame) {
        // A spawns B and destroys C IN THE SAME FRAME. Both go through the
        // deferred queues, which is the whole point of the exercise.
        eng::DeferredOps::QueueSpawn([&spawnCounter](eng::Scene& target) {
            const std::string name = "Stress" + std::to_string(++spawnCounter);
            const eng::EntityHandle handle = target.CreateEntity(name);
            if (eng::Entity* entity = target.Get(handle); entity != nullptr) {
                entity->Transform().SetLocalPosition(
                    eng::Vec2{static_cast<eng::f32>(spawnCounter % 100) * 8.0f, 0.0f});
            }
            return handle;
        });

        // Destroy the oldest stress entity, if there is one. Also queue a
        // DOUBLE DESTROY of it on purpose - two bullets hitting the same enemy
        // in one tick - which must be harmless.
        if (spawnCounter > 2) {
            const eng::EntityHandle victim =
                scene.Find("Stress" + std::to_string(spawnCounter - 2));
            eng::DeferredOps::QueueDestroy(victim);
            eng::DeferredOps::QueueDestroy(victim);   // deliberate double destroy
        }

        eng::Engine::Get().Simulate();
        eng::Engine::Get().RenderFrame();
        eng::Engine::Get().PresentFrame();

        if (frame == frames / 2) {
            ENGINE_LOG_INFO(eng::Channels::kGame,
                            "frame {}: {} entities, {} allocator bytes", frame + 1,
                            scene.EntityCount(), eng::MemorySystem::TotalBytesUsed());
        }
    }

    ENGINE_LOG_INFO(eng::Channels::kGame,
                    "frame {}: {} entities, {} allocator bytes (started at {} / {})",
                    frames, scene.EntityCount(), eng::MemorySystem::TotalBytesUsed(),
                    entitiesAtStart, bytesAtStart);
    // ALLOCATOR NUMBERS MUST BE STABLE, NOT MERELY NON-CRASHING. Current bytes
    // read zero at this sampling point because the frame stack is rewound at
    // the end of the render pass - that is the sawtooth, sampled at its
    // trough. PEAK is the number that would climb if the render pass were
    // leaking scratch, so it is the one worth reporting alongside.
    ENGINE_LOG_INFO(eng::Channels::kGame,
                    "peak allocator bytes {} (a peak that CLIMBS over the run is the leak "
                    "a stable current reading would hide)",
                    eng::MemorySystem::TotalPeakBytes());
    ENGINE_LOG_INFO(eng::Channels::kGame,
                    "spawned {} destroyed {}; sprite records {}; colliders {}",
                    eng::DeferredOps::TotalSpawned(), eng::DeferredOps::TotalDestroyed(),
                    eng::SpriteRenderSystem::Count(), eng::CollisionSystem::ColliderCount());
    ENGINE_LOG_INFO(eng::Channels::kGame,
                    "resource refcount {} (started at {}) - a leaked component would show "
                    "here as a texture nobody released",
                    eng::ResourceManager::TotalRefCount(), refsAtStart);

    eng::Engine::Get().Shutdown();
    return 0;
}

// WEEK 9 - the Milestone 3 verification, run headlessly so the evidence in
// docs/week09-milestone3.md is a paste rather than a description.
//
// Three things, in order: the per-resource refcount table with the scene
// loaded (18 entities share one texture, so that texture must read 18);
// the same table after Unload (must be empty, total zero); and a deliberately
// STALE handle, which must be detected and reported rather than dereferenced.
int RunMilestone3Check() {
    eng::Engine::Options options;
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    std::vector<eng::ResourceManager::Entry> entries;

    const auto dump = [&entries](const char* label) {
        eng::ResourceManager::Snapshot(entries);
        ENGINE_LOG_INFO(eng::Channels::kResource, "--- {} ---", label);
        ENGINE_LOG_INFO(eng::Channels::kResource, "{:<34} {:>5} {:>9} {:>8}", "virtual path",
                        "refs", "size", "state");
        for (const eng::ResourceManager::Entry& entry : entries) {
            ENGINE_LOG_INFO(eng::Channels::kResource, "{:<34} {:>5} {:>4}x{:<4} {:>8}",
                            entry.path, entry.refCount, entry.width, entry.height,
                            eng::ToString(entry.state));
        }
        ENGINE_LOG_INFO(eng::Channels::kResource,
                        "loaded {} | TOTAL REFCOUNT {} | {} bytes resident",
                        eng::ResourceManager::LoadedCount(),
                        eng::ResourceManager::TotalRefCount(),
                        eng::ResourceManager::BytesResident());
    };

    dump("scene loaded");

    eng::Engine::Get().GetScene().Unload();
    dump("after Unload()");

    // The stale-handle check. Acquire, keep the handle, release it to zero so
    // the slot is recycled, then use the handle we kept.
    ENGINE_LOG_INFO(eng::Channels::kResource, "--- stale handle ---");
    const eng::Handle<eng::Texture> stale =
        eng::ResourceManager::AcquireTexture("textures/checker_blue.bmp");
    ENGINE_LOG_INFO(eng::Channels::kResource, "acquired: index {} generation {}, valid={}",
                    stale.Index(), stale.Generation(),
                    eng::ResourceManager::IsValid(stale));
    eng::ResourceManager::Release(stale);
    ENGINE_LOG_INFO(eng::Channels::kResource,
                    "released. now dereferencing the handle we kept:");
    eng::Texture* dangling = eng::ResourceManager::Get(stale);
    ENGINE_LOG_INFO(eng::Channels::kResource,
                    "Get() returned {} - the engine is still running",
                    dangling == nullptr ? "nullptr" : "a placeholder");

    // --- scene discovery, and reloading after an unload --------------------
    // Both of these are what the editor's File menu drives. Checked here
    // because a menu is awkward to test by clicking and easy to test by
    // calling the same two functions it calls.
    ENGINE_LOG_INFO(eng::Channels::kScene, "--- scene discovery ---");
    std::vector<std::string> scenes;
    eng::FileSystem::ListFiles("scenes", ".json", scenes);
    for (const std::string& path : scenes) {
        ENGINE_LOG_INFO(eng::Channels::kScene, "  found: {}", path);
    }

    eng::Scene& scene = eng::Engine::Get().GetScene();
    ENGINE_LOG_INFO(eng::Channels::kScene,
                    "SourcePath() after Unload: '{}' (must NOT be empty, or Reload has "
                    "nothing to reload)", scene.SourcePath());

    std::string reloadError;
    if (eng::Engine::Get().LoadScene(scene.SourcePath(), reloadError)) {
        ENGINE_LOG_INFO(eng::Channels::kScene, "reload after unload: OK, {} entities",
                        scene.EntityCount());
    } else {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "reload after unload FAILED: {}",
                         reloadError);
    }

    // And loading a DIFFERENT scene, which is what Open Scene does.
    for (const std::string& path : scenes) {
        if (path != scene.SourcePath()) {
            std::string error;
            if (eng::Engine::Get().LoadScene(path, error)) {
                ENGINE_LOG_INFO(eng::Channels::kScene, "switched to '{}': {} entities",
                                path, scene.EntityCount());
            } else {
                ENGINE_LOG_ERROR(eng::Channels::kScene, "switch to '{}' FAILED: {}", path,
                                 error);
            }
            break;
        }
    }

    eng::Engine::Get().Shutdown();
    return 0;
}

// WEEK 6 - the Milestone 1 motion verification, run headlessly.
//
// M1 requires that "a three-deep parented hierarchy ORBITS and ROTATES
// correctly". That was claimed once on the strength of the transform unit tests
// while the scene was in fact completely static - nothing in the engine moved a
// transform, so orbit_test.json rendered 22 sprites that sat perfectly still.
// This is the check that would have caught it, so it exists now.
int RunMotionCheck() {
    eng::Engine::Options options;
    options.sceneOverride = "scenes/orbit_test.json";
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    eng::Scene& scene = eng::Engine::Get().GetScene();

    eng::Entity* root   = scene.Get(scene.Find("SolarRoot"));
    eng::Entity* planet = scene.Get(scene.Find("Planet"));
    eng::Entity* moon   = scene.Get(scene.Find("Moon"));
    if (root == nullptr || planet == nullptr || moon == nullptr) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "the three-deep hierarchy is not present");
        eng::Engine::Get().Shutdown();
        return 1;
    }

    ENGINE_LOG_INFO(eng::Channels::kScene, "depths: SolarRoot {}, Planet {}, Moon {}",
                    root->Transform().Depth(), planet->Transform().Depth(),
                    moon->Transform().Depth());

    const auto sample = [&](const char* label) {
        const eng::Vec2 p = planet->Transform().WorldPosition();
        const eng::Vec2 m = moon->Transform().WorldPosition();
        ENGINE_LOG_INFO(eng::Channels::kScene,
                        "{:<10} planet ({:8.2f}, {:8.2f})  moon ({:8.2f}, {:8.2f})  "
                        "|moon-planet| {:.2f}",
                        label, p.x, p.y, m.x, m.y, eng::Distance(p, m));
    };

    const eng::Vec2 planetStart = planet->Transform().WorldPosition();
    const eng::Vec2 moonStart   = moon->Transform().WorldPosition();
    const eng::f32  radiusStart = eng::Distance(planetStart, moonStart);
    const eng::f32  orbitStart  = planetStart.Length();

    sample("frame 0");
    for (int i = 0; i < 5; ++i) {
        for (int f = 0; f < 30; ++f) {
            eng::Engine::Get().BeginFrame();
            eng::Engine::Get().Simulate();
            eng::Engine::Get().RenderFrame();
            eng::Engine::Get().PresentFrame();
        }
        sample("+30 frames");
    }

    const eng::Vec2 planetEnd = planet->Transform().WorldPosition();
    const eng::Vec2 moonEnd   = moon->Transform().WorldPosition();

    // THE TWO PROPERTIES THAT MAKE IT AN ORBIT rather than a drift: the planet
    // MOVED, and its distance from the origin did NOT change. Same for the moon
    // about the planet. A hierarchy that translated instead of rotating would
    // pass the first and fail the second.
    const eng::f32 planetMoved  = eng::Distance(planetStart, planetEnd);
    const eng::f32 orbitEnd     = planetEnd.Length();
    const eng::f32 radiusEnd    = eng::Distance(planetEnd, moonEnd);

    ENGINE_LOG_INFO(eng::Channels::kScene, "planet moved {:.2f} units", planetMoved);
    ENGINE_LOG_INFO(eng::Channels::kScene,
                    "planet orbit radius {:.3f} -> {:.3f}  (must be unchanged)",
                    orbitStart, orbitEnd);
    ENGINE_LOG_INFO(eng::Channels::kScene,
                    "moon radius about planet {:.3f} -> {:.3f}  (must be unchanged)",
                    radiusStart, radiusEnd);
    ENGINE_LOG_INFO(eng::Channels::kScene, "spin components active: {}",
                    eng::SpinSystem::Count());

    const bool moved     = planetMoved > 1.0f;
    const bool orbits    = std::fabs(orbitEnd - orbitStart) < 0.5f;
    const bool moonOrbits = std::fabs(radiusEnd - radiusStart) < 0.5f;
    ENGINE_LOG_INFO(eng::Channels::kScene,
                    "MOTION CHECK: moved={} planet-orbits={} moon-orbits-planet={}",
                    moved, orbits, moonOrbits);

    eng::Engine::Get().Shutdown();
    return (moved && orbits && moonOrbits) ? 0 : 1;
}

// The scene save/load ROUND TRIP, verified rather than assumed.
//
// Loads a scene, saves it somewhere else, loads THAT, and compares. The whole
// point of Serialize is that `load -> save -> load` is a fixed point, and the
// only way to know it is one is to run it - a Serialize that quietly writes the
// wrong key produces a file that loads without complaint and has lost a field.
int RunSaveCheck(const std::string& scenePath) {
    eng::Engine::Options options;
    options.sceneOverride = scenePath;
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    eng::Scene& scene = eng::Engine::Get().GetScene();

    // A fingerprint of everything that ought to survive the trip.
    struct Snapshot {
        std::string name;
        std::string parent;
        eng::usize  componentCount = 0;
        eng::Vec2   localPosition{};
        eng::f32    localRotation = 0.0f;
        eng::Vec2   localScale{};
        eng::Vec2   worldPosition{};
    };

    const auto capture = [](eng::Scene& target) {
        std::vector<Snapshot> out;
        std::unordered_map<const eng::Transform2D*, std::string> names;
        target.ForEach([&](eng::Entity& e) { names[&e.Transform()] = e.Name(); });
        target.ForEach([&](eng::Entity& e) {
            Snapshot s;
            s.name           = e.Name();
            s.componentCount = e.ComponentCount();
            s.localPosition  = e.Transform().LocalPosition();
            s.localRotation  = e.Transform().LocalRotation();
            s.localScale     = e.Transform().LocalScale();
            s.worldPosition  = e.Transform().WorldPosition();
            if (const eng::Transform2D* p = e.Transform().Parent(); p != nullptr) {
                if (const auto it = names.find(p); it != names.end()) {
                    s.parent = it->second;
                }
            }
            out.push_back(std::move(s));
        });
        std::sort(out.begin(), out.end(),
                  [](const Snapshot& a, const Snapshot& b) { return a.name < b.name; });
        return out;
    };

    const std::vector<Snapshot> before = capture(scene);
    const eng::u64 refsBefore = eng::ResourceManager::TotalRefCount();
    ENGINE_LOG_INFO(eng::Channels::kScene, "captured {} entities, refcount {}",
                    before.size(), refsBefore);

    const std::string roundTripPath = "scenes/_roundtrip_check.json";
    std::string       error;
    if (!eng::Engine::Get().SaveScene(roundTripPath, error)) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "save failed: {}", error);
        eng::Engine::Get().Shutdown();
        return 1;
    }

    if (!eng::Engine::Get().LoadScene(roundTripPath, error)) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "reload failed: {}", error);
        eng::Engine::Get().Shutdown();
        return 1;
    }

    const std::vector<Snapshot> after = capture(scene);
    const eng::u64 refsAfter = eng::ResourceManager::TotalRefCount();

    int mismatches = 0;
    if (before.size() != after.size()) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "entity count {} -> {}", before.size(),
                         after.size());
        ++mismatches;
    } else {
        for (eng::usize i = 0; i < before.size(); ++i) {
            const Snapshot& a = before[i];
            const Snapshot& b = after[i];
            const bool same =
                a.name == b.name && a.parent == b.parent &&
                a.componentCount == b.componentCount &&
                eng::ApproxEqual(a.localPosition, b.localPosition, 0.001f) &&
                eng::ApproxEqual(a.localRotation, b.localRotation, 0.001f) &&
                eng::ApproxEqual(a.localScale, b.localScale, 0.001f) &&
                eng::ApproxEqual(a.worldPosition, b.worldPosition, 0.01f);
            if (!same) {
                ENGINE_LOG_ERROR(eng::Channels::kScene,
                                 "MISMATCH '{}' vs '{}': parent '{}'/'{}' components {}/{} "
                                 "pos ({:.3f},{:.3f})/({:.3f},{:.3f}) rot {:.4f}/{:.4f}",
                                 a.name, b.name, a.parent, b.parent, a.componentCount,
                                 b.componentCount, a.localPosition.x, a.localPosition.y,
                                 b.localPosition.x, b.localPosition.y, a.localRotation,
                                 b.localRotation);
                ++mismatches;
            }
        }
    }

    if (refsBefore != refsAfter) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "resource refcount {} -> {}", refsBefore,
                         refsAfter);
        ++mismatches;
    }

    ENGINE_LOG_INFO(eng::Channels::kScene,
                    "ROUND TRIP: {} entities, {} mismatch(es), refcount {} -> {} -- {}",
                    after.size(), mismatches, refsBefore, refsAfter,
                    mismatches == 0 ? "PASS" : "FAIL");

    // TIDY UP THE TEMP FILE. Left behind, it lands in assets/scenes/ - which
    // is a DISCOVERED directory, so it appears in the editor's Open Scene menu
    // and in every other check that walks the scene folder. A verification run
    // that changes what the next run sees is not a verification run.
    //
    // Only on PASS: on failure the file is the evidence, and deleting the one
    // artifact that would let someone diff it against the original would be
    // exactly the wrong instinct.
    if (mismatches == 0) {
        std::error_code ec;
        std::filesystem::remove(eng::FileSystem::Resolve(roundTripPath), ec);
        if (ec) {
            ENGINE_LOG_WARN(eng::Channels::kScene, "could not remove '{}': {}",
                            roundTripPath, ec.message());
        }
    } else {
        ENGINE_LOG_WARN(eng::Channels::kScene,
                        "'{}' KEPT for inspection - diff it against the original",
                        roundTripPath);
    }

    eng::Engine::Get().Shutdown();
    return mismatches == 0 ? 0 : 1;
}

// THE PLAY-MODE SNAPSHOT CHECK - the thing that makes pressing Play safe.
//
// Unity's contract, and now this editor's: Play snapshots the scene, Stop puts
// it back exactly as it was, so an hour of editing is not destroyed by a play
// session that moved the player and destroyed half the pickups.
//
// The check has THREE parts, and the middle one is the one people forget:
//
//   1. after Stop, the scene matches what it was before Play;
//   2. DURING play, the scene actually DIFFERED - otherwise part 1 passes for
//      the wrong reason, and a Play button that did nothing at all would score
//      a perfect result;
//   3. the resource refcount comes back to where it started, because a restore
//      that leaks a texture per play session is a restore that kills a long
//      editing session by inches.
// THE SCRIPT CHECK - four claims the script system makes, tested headlessly.
//
//   1. a script attached by NAME resolves to a live behaviour and RUNS;
//   2. a name that is not compiled in attaches anyway, reports itself
//      unresolved, and does not crash anything;
//   3. BOTH survive a save/load round trip - including the unresolved one,
//      because dropping it from the save would silently delete the author's
//      work every time they saved from a build that lacked their script;
//   4. OnDestroy runs when the entity goes away.
//
// Claim 2 is the one worth having a test for. It is the whole reason the
// component stores a name rather than a type, and it is the behaviour that is
// easiest to "fix" into a refusal by someone who has not read why.
int RunScriptCheck() {
    eng::Engine::Options options;
    options.sceneOverride = "scenes/orbit_test.json";
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    eng::Engine& engine = eng::Engine::Get();
    eng::Scene&  scene  = engine.GetScene();

    ENGINE_LOG_INFO(eng::Channels::kScene, "{} script(s) compiled into this build",
                    eng::ScriptRegistry::Count());

    int failures = 0;
    const auto require = [&failures](bool condition, const char* what) {
        ENGINE_LOG_INFO(eng::Channels::kScene, "  {:<58} {}", what,
                        condition ? "ok" : "FAILED");
        if (!condition) {
            ++failures;
        }
    };

    const auto attachScript = [&scene](const char* entityName, const char* scriptName) {
        const eng::EntityHandle handle = scene.CreateEntity(entityName);
        eng::Entity*            entity = scene.Get(handle);
        if (entity == nullptr) {
            return static_cast<eng::ScriptComponent*>(nullptr);
        }
        auto* script = static_cast<eng::ScriptComponent*>(
            entity->AddComponent(eng::ScriptComponent::kTypeName));
        if (script != nullptr) {
            script->SetScriptName(scriptName);
        }
        return script;
    };

    const auto step = [](int frames) {
        for (int i = 0; i < frames; ++i) {
            eng::Engine::Get().BeginFrame();
            eng::Engine::Get().Simulate();
            eng::Engine::Get().RenderFrame();
            eng::Engine::Get().PresentFrame();
        }
    };

    // ---- 1. a compiled script resolves and runs -------------------------
    ENGINE_LOG_INFO(eng::Channels::kScene, "--- a script that IS compiled in ---");

    eng::ScriptComponent* live = attachScript("ScriptProbe", "Orbiter");
    require(live != nullptr, "ScriptComponent attaches");
    require(live != nullptr && live->IsResolved(), "'Orbiter' resolves to a behaviour");

    const eng::EntityHandle probe = live != nullptr ? live->OwnerHandle() : eng::EntityHandle{};
    if (eng::Entity* entity = scene.Get(probe); entity != nullptr) {
        entity->Transform().SetLocalPosition(eng::Vec2{500.0f, 500.0f});
    }
    const eng::Vec2 before = scene.Get(probe)->Transform().WorldPosition();

    step(40);

    const eng::Vec2 after = scene.Get(probe)->Transform().WorldPosition();
    ENGINE_LOG_INFO(eng::Channels::kScene, "  moved ({:.2f},{:.2f}) -> ({:.2f},{:.2f})",
                    before.x, before.y, after.x, after.y);
    // The Orbiter script moves its entity in a circle. A script that resolved
    // but never ticked would leave this unchanged, which is exactly the
    // failure a "does it resolve" test alone would miss.
    require(!eng::ApproxEqual(before, after, 0.01f), "OnUpdate actually ran (it moved)");

    // ---- 2. an uncompiled script attaches, unresolved --------------------
    ENGINE_LOG_INFO(eng::Channels::kScene, "--- a script that is NOT compiled in ---");

    eng::ScriptComponent* pending = attachScript("PendingProbe", "NotCompiledYet");
    require(pending != nullptr, "an unknown script still attaches");
    require(pending != nullptr && !pending->IsResolved(), "and reports itself unresolved");
    require(eng::ScriptSystem::UnresolvedCount() == 1, "UnresolvedCount reports exactly 1");

    step(10);   // must not crash, must not tick a null behaviour

    // ---- 3. both survive a save/load round trip --------------------------
    ENGINE_LOG_INFO(eng::Channels::kScene, "--- save / load round trip ---");

    const std::string path = "scenes/_script_check.json";
    std::string       error;
    require(engine.SaveScene(path, error), "the scene saves with both scripts");
    if (!error.empty()) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "save: {}", error);
    }

    require(engine.LoadScene(path, error), "and loads again");

    const auto scriptNameOn = [&scene](const char* entityName) -> std::string {
        eng::Entity* entity = scene.Get(scene.Find(entityName));
        if (entity == nullptr) {
            return "<no entity>";
        }
        auto* script = entity->Find<eng::ScriptComponent>();
        return script != nullptr ? script->ScriptName() : "<no script>";
    };

    require(scriptNameOn("ScriptProbe") == "Orbiter", "the resolved binding came back");
    require(scriptNameOn("PendingProbe") == "NotCompiledYet",
            "the UNRESOLVED binding came back too");

    // ---- 4. OnDestroy runs ----------------------------------------------
    // Not asserted directly - the behaviour logs it - but destroying and
    // stepping proves the teardown path does not crash and that the system
    // deregisters, which the count check below shows.
    ENGINE_LOG_INFO(eng::Channels::kScene, "--- destruction ---");
    const eng::usize scriptsBefore = eng::ScriptSystem::Count();
    eng::DeferredOps::QueueDestroy(scene.Find("ScriptProbe"));
    step(5);
    require(eng::ScriptSystem::Count() == scriptsBefore - 1,
            "destroying the entity deregisters its script");

    // Tidy up, for the same reason --save-check does: assets/scenes/ is a
    // DISCOVERED directory, and a check that leaves a file behind changes what
    // the next run and the editor's Open Scene menu see.
    if (failures == 0) {
        std::error_code ec;
        std::filesystem::remove(eng::FileSystem::Resolve(path), ec);
    } else {
        ENGINE_LOG_WARN(eng::Channels::kScene, "'{}' KEPT for inspection", path);
    }

    ENGINE_LOG_INFO(eng::Channels::kScene, "SCRIPTS: {} failure(s) -- {}", failures,
                    failures == 0 ? "PASS" : "FAIL");

    engine.Shutdown();
    return failures == 0 ? 0 : 1;
}

int RunPlayModeCheck() {
    eng::Engine::Options options;
    options.sceneOverride = "scenes/collector.json";
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    eng::Engine& engine = eng::Engine::Get();
    eng::Scene&  scene  = engine.GetScene();

    // Name + world position + component count, sorted, which is enough to catch
    // a moved entity, a destroyed one, a spawned one, and a lost component.
    const auto capture = [](eng::Scene& target) {
        std::vector<std::string> out;
        target.ForEach([&](eng::Entity& e) {
            const eng::Vec2 p = e.Transform().WorldPosition();
            out.push_back(std::format("{}|{:.3f},{:.3f}|{}", e.Name(), p.x, p.y,
                                      e.ComponentCount()));
        });
        std::sort(out.begin(), out.end());
        return out;
    };

    const auto step = [](int frames) {
        for (int i = 0; i < frames; ++i) {
            eng::Engine::Get().BeginFrame();
            eng::Engine::Get().Simulate();
            eng::Engine::Get().RenderFrame();
            eng::Engine::Get().PresentFrame();
        }
    };

    const std::vector<std::string> before     = capture(scene);
    const eng::u64                 refsBefore = eng::ResourceManager::TotalRefCount();
    ENGINE_LOG_INFO(eng::Channels::kScene, "EDIT MODE: {} entities, refcount {}",
                    before.size(), refsBefore);

    std::string error;
    if (!engine.EnterPlayMode(error)) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "EnterPlayMode refused: {}", error);
        engine.Shutdown();
        return 1;
    }

    // Wreck the place, the way a play session does. Move something, destroy
    // several things, and spawn something that was never in the file.
    eng::Entity* player = scene.Get(scene.Find("Player"));
    if (player != nullptr) {
        player->Transform().SetLocalPosition(eng::Vec2{777.0f, -333.0f});
    }

    int destroyed = 0;
    scene.ForEach([&](eng::Entity& e) {
        if (e.Name().starts_with("Pickup") && destroyed < 3) {
            eng::DeferredOps::QueueDestroy(e.Handle());
            ++destroyed;
        }
    });
    scene.CreateEntity("SpawnedDuringPlay");

    step(20);

    const std::vector<std::string> during     = capture(scene);
    const eng::u64                 refsDuring = eng::ResourceManager::TotalRefCount();
    ENGINE_LOG_INFO(eng::Channels::kScene,
                    "PLAY MODE: {} entities, refcount {} ({} destroyed, 1 spawned, "
                    "Player moved)",
                    during.size(), refsDuring, destroyed);

    engine.ExitPlayMode();

    const std::vector<std::string> after     = capture(scene);
    const eng::u64                 refsAfter = eng::ResourceManager::TotalRefCount();

    int mismatches = 0;

    // Part 2 first, because it decides whether part 1 means anything.
    if (during == before) {
        ENGINE_LOG_ERROR(eng::Channels::kScene,
                         "the scene did NOT change during play - this check proves "
                         "nothing as written");
        ++mismatches;
    }

    if (after.size() != before.size()) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "entity count {} -> {} -> {}",
                         before.size(), during.size(), after.size());
        ++mismatches;
    } else {
        for (eng::usize i = 0; i < before.size(); ++i) {
            if (before[i] != after[i]) {
                ENGINE_LOG_ERROR(eng::Channels::kScene, "NOT RESTORED: '{}' came back as '{}'",
                                 before[i], after[i]);
                ++mismatches;
            }
        }
    }

    if (refsBefore != refsAfter) {
        ENGINE_LOG_ERROR(eng::Channels::kScene, "resource refcount {} -> {} (leaked {})",
                         refsBefore, refsAfter,
                         static_cast<eng::i64>(refsAfter) - static_cast<eng::i64>(refsBefore));
        ++mismatches;
    }

    ENGINE_LOG_INFO(eng::Channels::kScene,
                    "PLAY/STOP: {} entities before, {} during, {} after; refcount "
                    "{} -> {} -> {}; {} mismatch(es) -- {}",
                    before.size(), during.size(), after.size(), refsBefore, refsDuring,
                    refsAfter, mismatches, mismatches == 0 ? "PASS" : "FAIL");

    engine.Shutdown();
    return mismatches == 0 ? 0 : 1;
}

// WEEK 10 - the Milestone 4 collision verification, run headlessly.
//
// Checks, in order: BOTH entities receive an event naming the correct partner;
// ENTER fires once rather than every frame; STAY fires while overlapping; EXIT
// fires on separation; and a LAYER MASK suppresses a pair that would otherwise
// collide, TOGGLED AT RUNTIME VIA A CVAR.
//
// Built entirely from the engine's public API - CreateEntity, AddComponent,
// SubscribeBroadcast - which is the same surface the gate exercise uses.
int RunCollisionCheck() {
    eng::Engine::Options options;
    options.sceneOverride = "scenes/collector.json";
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    eng::Scene& scene = eng::Engine::Get().GetScene();

    // Two overlapping boxes, one Player and one Pickup, built from code rather
    // than from the scene file so the test controls their positions.
    const eng::EntityHandle a = scene.CreateEntity("ProbeA");
    const eng::EntityHandle b = scene.CreateEntity("ProbeB");

    auto* colliderA = static_cast<eng::AABBColliderComponent*>(
        scene.Get(a)->AddComponent(eng::AABBColliderComponent::kTypeName));
    auto* colliderB = static_cast<eng::AABBColliderComponent*>(
        scene.Get(b)->AddComponent(eng::AABBColliderComponent::kTypeName));

    colliderA->SetHalfExtents(eng::Vec2{20.0f, 20.0f});
    colliderA->SetLayer(eng::CollisionLayers::kPlayer);
    colliderA->SetMask(eng::CollisionLayers::kPickup);

    colliderB->SetHalfExtents(eng::Vec2{20.0f, 20.0f});
    colliderB->SetLayer(eng::CollisionLayers::kPickup);
    colliderB->SetMask(eng::CollisionLayers::kPlayer);

    // Placed far from anything the scene file put down. The first version of
    // this check put the probes at the origin, where the scene's own Player
    // sits, and the run reported enter=4 instead of enter=2 - two pairs, not
    // one. That was the collision system being right and the test being
    // ambiguous, which is its own small lesson about writing a check that
    // isolates the thing it is checking.
    constexpr eng::Vec2 kFarAway{4000.0f, 4000.0f};
    scene.Get(a)->Transform().SetLocalPosition(kFarAway);
    scene.Get(b)->Transform().SetLocalPosition(kFarAway + eng::Vec2{10.0f, 0.0f});

    int enterCount = 0, stayCount = 0, exitCount = 0;

    const auto describe = [&scene](eng::EntityHandle handle) {
        eng::Entity* entity = scene.Get(handle);
        return entity != nullptr ? entity->Name() : std::string("<gone>");
    };

    eng::MessageBus::SubscribeBroadcast(
        eng::MessageTypes::CollisionEnter(), [&](const eng::Message& message) {
            ++enterCount;
            ENGINE_LOG_INFO(eng::Channels::kPhysics, "ENTER  target={:<8} partner={}",
                            describe(message.target), describe(message.other));
        });
    eng::MessageBus::SubscribeBroadcast(
        eng::MessageTypes::CollisionStay(),
        [&](const eng::Message&) { ++stayCount; });
    eng::MessageBus::SubscribeBroadcast(
        eng::MessageTypes::CollisionExit(), [&](const eng::Message& message) {
            ++exitCount;
            ENGINE_LOG_INFO(eng::Channels::kPhysics, "EXIT   target={:<8} partner={}",
                            describe(message.target), describe(message.other));
        });

    const auto step = [](int frames) {
        for (int i = 0; i < frames; ++i) {
            eng::Engine::Get().BeginFrame();
            eng::Engine::Get().Simulate();
            eng::Engine::Get().RenderFrame();
            eng::Engine::Get().PresentFrame();
        }
    };

    ENGINE_LOG_INFO(eng::Channels::kPhysics, "--- overlapping for 30 frames ---");
    step(30);
    ENGINE_LOG_INFO(eng::Channels::kPhysics,
                    "enter={} stay={} exit={}   (enter must be 2 - one per entity - "
                    "and must NOT repeat)", enterCount, stayCount, exitCount);

    ENGINE_LOG_INFO(eng::Channels::kPhysics, "--- separating ---");
    scene.Get(b)->Transform().SetLocalPosition(kFarAway + eng::Vec2{500.0f, 0.0f});
    step(10);
    ENGINE_LOG_INFO(eng::Channels::kPhysics, "enter={} stay={} exit={}   (exit must be 2)",
                    enterCount, stayCount, exitCount);

    // ---- THE LAYER MASK TOGGLE, VIA A CVAR, AT RUNTIME -------------------
    ENGINE_LOG_INFO(eng::Channels::kPhysics, "--- re-overlapping with the mask ON ---");
    enterCount = stayCount = exitCount = 0;
    scene.Get(b)->Transform().SetLocalPosition(kFarAway + eng::Vec2{10.0f, 0.0f});
    step(10);
    ENGINE_LOG_INFO(eng::Channels::kPhysics, "enter={} stay={} (collisions happening)",
                    enterCount, stayCount);

    ENGINE_LOG_INFO(eng::Channels::kPhysics,
                    "--- clearing CVar physics.playerCollidesWithPickups ---");
    eng::CVar* toggle = eng::CVarRegistry::Find("physics.playerCollidesWithPickups");
    toggle->SetBool(false);
    // The gameplay-side response to the CVar: drop Pickup out of the player's
    // mask. The engine does not do this for you - a CVar is a value, and what
    // it means is the caller's business.
    colliderA->SetMask(colliderA->Mask() & ~eng::CollisionLayers::kPickup);

    enterCount = stayCount = exitCount = 0;
    step(10);
    ENGINE_LOG_INFO(eng::Channels::kPhysics,
                    "enter={} stay={} exit={}   (all must be 0 - the pair is suppressed "
                    "while still geometrically overlapping)",
                    enterCount, stayCount, exitCount);

    ENGINE_LOG_INFO(eng::Channels::kPhysics, "--- setting the CVar back ---");
    toggle->SetBool(true);
    colliderA->SetMask(colliderA->Mask() | eng::CollisionLayers::kPickup);
    enterCount = stayCount = exitCount = 0;
    step(10);
    ENGINE_LOG_INFO(eng::Channels::kPhysics,
                    "enter={} stay={} (collisions resume, no rebuild, no relaunch)",
                    enterCount, stayCount);

    eng::Engine::Get().Shutdown();
    return 0;
}

// WEEK 7 - the allocator HUD as debug TEXT. The editor's Memory panel replaced
// this and is far better, but the sandbox has no ImGui, and the sandbox is
// what ships. Both were kept, which the Week 7 patch explicitly allows.
void DrawAllocatorHud() {
    if (!eng::CVarRegistry::GetBool("debug.showAllocatorHud", true)) {
        return;
    }

    char  line[128];
    float y = 16.0f;

    std::snprintf(line, sizeof(line), "MEM  %zu / %zu bytes   peak %zu",
                  eng::MemorySystem::TotalBytesUsed(),
                  eng::MemorySystem::TotalBytesCapacity(),
                  eng::MemorySystem::TotalPeakBytes());
    eng::DebugDraw::Text(eng::Vec2{16.0f, y}, line, eng::Color::Cyan(), 0.0f,
                         eng::DebugSpace::Screen);
    y += 18.0f;

    eng::MemorySystem::ForEachStack([&](eng::StackAllocator& allocator) {
        std::snprintf(line, sizeof(line), "  %-12s %8zu / %8zu  peak %8zu  allocs %zu",
                      allocator.Name(), allocator.BytesUsed(), allocator.BytesCapacity(),
                      allocator.PeakBytes(), allocator.AllocationCount());
        eng::DebugDraw::Text(eng::Vec2{16.0f, y}, line, eng::Color::Cyan(), 0.0f,
                             eng::DebugSpace::Screen);
        y += 16.0f;
    });

    eng::MemorySystem::ForEachPool([&](eng::PoolAllocator& allocator) {
        std::snprintf(line, sizeof(line), "  %-12s %8zu / %8zu blocks  peak %zu",
                      allocator.Name(), allocator.BlocksInUse(), allocator.BlockCount(),
                      allocator.PeakBlocksInUse());
        eng::DebugDraw::Text(eng::Vec2{16.0f, y}, line, eng::Color::Cyan(), 0.0f,
                             eng::DebugSpace::Screen);
        y += 16.0f;
    });

    std::snprintf(line, sizeof(line), "RES  %zu resident   total refcount %llu",
                  eng::ResourceManager::LoadedCount(),
                  static_cast<unsigned long long>(eng::ResourceManager::TotalRefCount()));
    eng::DebugDraw::Text(eng::Vec2{16.0f, y}, line, eng::Color::Cyan(), 0.0f,
                         eng::DebugSpace::Screen);
}

} // namespace

int main(int argc, char** argv) {
    eng::Engine::Options options;
    bool runGame   = false;
    bool autoplay  = false;
    // 0 means "until the window is closed". A positive value exits cleanly
    // after N frames, which is what lets an automated run produce a COMPLETE
    // log: killing the process leaves the file sink's last buffered lines
    // unwritten, and the interesting part of any run is the end of it.
    int  frameLimit = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const char* next = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
        if (arg == "--sizeof-audit") {
            eng::Log::Init({}, eng::LogLevel::Info);
            bench::RunSizeofAudit();
            eng::Log::Shutdown();
            return 0;
        }
        if (arg == "--layout-bench") {
            // Standalone: no window, no subsystems. The benchmark measures
            // memory layout and nothing else, and booting a renderer to run it
            // would add noise for no reason.
            eng::Log::Init({}, eng::LogLevel::Info);
            const eng::usize count =
                (next != nullptr) ? static_cast<eng::usize>(std::atoll(next)) : 100000u;
            bench::RunLayoutBenchmark(count, 200);
            eng::Log::Shutdown();
            return 0;
        }
        if (arg == "--os-measure") {
            eng::Log::Init({}, eng::LogLevel::Info);
            const eng::JobSystem::PlatformCosts costs = bench::RunPlatformMeasurements();
            eng::JobSystem::SetPlatformCosts(costs);
            eng::Log::Shutdown();
            return 0;
        }
        if (arg == "--random-check") {
            const eng::u64 seed =
                (next != nullptr) ? static_cast<eng::u64>(std::atoll(next)) : 12345ull;
            return RunRandomCheck(seed);
        }
        if (arg == "--save-check") {
            return RunSaveCheck(next != nullptr ? next : "scenes/orbit_test.json");
        }
        if (arg == "--motion-check") {
            return RunMotionCheck();
        }
        if (arg == "--m3-check") {
            return RunMilestone3Check();
        }
        if (arg == "--script-check") {
            return RunScriptCheck();
        }
        if (arg == "--playmode-check") {
            return RunPlayModeCheck();
        }
        if (arg == "--collision-check") {
            return RunCollisionCheck();
        }
        if (arg == "--stress") {
            const int frames = (next != nullptr) ? std::atoi(next) : 1000;
            return RunStress(frames);
        }
        if (arg == "--game") {
            runGame = true;
            continue;
        }
        if (arg == "--autoplay") {
            runGame  = true;
            autoplay = true;
            continue;
        }
        if (arg == "--frames" && next != nullptr) {
            frameLimit = std::atoi(next);
            ++i;
            continue;
        }
        if (arg == "--scene" && next != nullptr) {
            options.sceneOverride = next;
            ++i;
            continue;
        }
        if (arg == "--config" && next != nullptr) {
            options.configPath = next;
            ++i;
            continue;
        }
        if (arg == "--fail-subsystem" && next != nullptr) {
            options.forceFailSubsystem = next;
            ++i;
            continue;
        }
        std::fprintf(stderr, "unknown argument '%s'\n", arg.c_str());
        PrintUsage();
        return 2;
    }

    if (runGame && options.sceneOverride.empty()) {
        options.sceneOverride = "scenes/collector.json";
    }

    if (!eng::Engine::Get().Init(options)) {
        // WEEK 7: the forced-failure path arrives here. Everything that came
        // up has already been torn down in reverse order, nothing after the
        // failure was started, and the exit code is non-zero with a readable
        // message - which is the graded checklist.
        std::fprintf(stderr,
                     "sandbox: initialisation failed. The boot log above names the "
                     "subsystem that refused to start.\n");
        eng::Engine::Get().Shutdown();
        return 1;
    }

    game::CollectorGame collector;
    if (runGame) {
        if (!collector.Init()) {
            eng::Engine::Get().Shutdown();
            return 1;
        }
        collector.SetAutopilot(autoplay);
    }

    int frame = 0;
    while (eng::Engine::Get().BeginFrame()) {
        eng::Engine::Get().Simulate();
        DrawAllocatorHud();
        eng::Engine::Get().RenderFrame();
        eng::Engine::Get().PresentFrame();

        if (frameLimit > 0 && ++frame >= frameLimit) {
            ENGINE_LOG_INFO(eng::Channels::kGame, "frame limit ({}) reached; exiting",
                            frameLimit);
            break;
        }
        // An autoplayed round exits when it is over rather than sitting on the
        // win screen, so it can be scripted.
        if (autoplay && collector.IsFinished()) {
            ENGINE_LOG_INFO(eng::Channels::kGame,
                            "autoplay finished after {} frames with {} collected", frame,
                            collector.Collected());
            break;
        }
    }

    if (runGame) {
        collector.Shutdown();
    }
    eng::Engine::Get().Shutdown();
    std::printf("Clean exit.\n");
    return 0;
}
