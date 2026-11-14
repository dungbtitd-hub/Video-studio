// Plugin API (beta): manifests are read at start-up, the library opens on first use,
// broken plugins are reported, never fatal.
#include <filesystem>
#include <fstream>

#include "vstudio/audio_fx.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/plugins.hpp"
#include "vtest.hpp"

using namespace vstudio;
namespace fs = std::filesystem;

#ifndef VSTUDIO_TEST_PLUGIN_DIR
#define VSTUDIO_TEST_PLUGIN_DIR ""
#endif

TEST("plugins: example plugin loads lazily and applies its effects") {
    if (!plugins::supported() || std::string(VSTUDIO_TEST_PLUGIN_DIR).empty()) return;
    ModuleRegistry r;
    const auto res = plugins::loadPlugins(r, VSTUDIO_TEST_PLUGIN_DIR);
    CHECK(res.errors.empty());
    CHECK_EQ(res.plugins.size(), std::size_t(1));
    CHECK_EQ(res.plugins[0].effects.size(), std::size_t(2));
    CHECK(r.has("example.invert"));
    CHECK(!r.loaded("example.invert"));  // manifest only, the library is still closed

    std::string err;
    auto fx = r.get<IVideoEffect>("example.invert", &err);
    CHECK(fx != nullptr);
    VideoFrame f;
    f.allocate(4, 2);
    for (std::size_t i = 0; i < f.rgba.size(); i += 4) f.rgba[i] = 10, f.rgba[i + 1] = 200, f.rgba[i + 3] = 255;
    fx->apply(f, Json{{"amount", 1.0}}, Time{});
    CHECK_EQ(int(f.rgba[0]), 245);
    CHECK_EQ(int(f.rgba[1]), 55);
    CHECK_EQ(int(f.rgba[3]), 255);  // alpha untouched

    auto gain = r.get<IAudioEffect>("example.gain", &err);
    CHECK(gain != nullptr);
    AudioBuffer b;
    b.samples = {0.5f, -0.5f, 1.0f, 0.0f};
    gain->apply(b, Json{{"gain", 0.5}});
    CHECK_NEAR(b.samples[0], 0.25, 1e-6);
    CHECK_NEAR(b.samples[2], 0.5, 1e-6);
    CHECK_EQ(gain->margin(Json::object(), 48000), std::int64_t(0));
}

TEST("plugins: broken manifests and libraries are reported, never fatal") {
    if (!plugins::supported()) return;
    const fs::path dir = fs::temp_directory_path() / "vs-plugins-broken";
    fs::remove_all(dir);
    fs::create_directories(dir / "bad-json");
    fs::create_directories(dir / "old-abi");
    fs::create_directories(dir / "no-lib");
    std::ofstream(dir / "bad-json" / "plugin.json") << "{ not json";
    std::ofstream(dir / "old-abi" / "plugin.json") << R"({"abi": 99, "id": "old"})";
    std::ofstream(dir / "no-lib" / "plugin.json")
        << R"({"abi": 1, "id": "nolib", "library": {"windows": "x.dll", "macos": "libx.dylib", "linux": "libx.so"},
              "video_effects": [{"id": "nolib.fx"}, {"id": "builtin.brightness"}]})";
    ModuleRegistry r;
    registerBuiltinModules(r);
    const auto res = plugins::loadPlugins(r, dir.string());
    CHECK_EQ(res.errors.size(), std::size_t(3));  // bad json, old abi, reserved id
    CHECK(r.has("nolib.fx"));
    std::string err;
    CHECK(r.get<IVideoEffect>("nolib.fx", &err) == nullptr);  // library missing: error, no crash
    CHECK(err.find("cannot load") != std::string::npos);
    CHECK(plugins::loadPlugins(r, (dir / "missing").string()).plugins.empty());
    fs::remove_all(dir);
}
