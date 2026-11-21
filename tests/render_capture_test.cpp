// Render capture / parity test.
//
// Renders a fixed set of views (every map from two fixed camera poses, plus
// menu screens) and writes each frame as a binary PPM. Used to prove that a
// renderer refactor or a new backend draws the same image. The backend follows
// the game (CUBE_RENDER_API=opengl|vulkan selects it):
//
//   render_capture_test capture <outdir>
//   render_capture_test compare <refdir> <outdir> [maxMeanDiff]
//
// compare exits non-zero when any image differs in size, or its mean absolute
// channel difference exceeds maxMeanDiff (default 0.5 of 255), and prints the
// share of pixels that differ by more than 8/255 for every image.
//
// Needs a window and must run from a directory with assets/.

#include "core/game.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb;
};

// Render one frame and read it back through the backend (before present).
Image renderAndCapture(Game& game) {
    Image img;
    game.debugRenderer().requestCapture();
    game.render();
    CapturedImage frame;
    if (game.debugRenderer().takeCapture(frame)) {
        img.w = frame.width;
        img.h = frame.height;
        img.rgb = std::move(frame.rgb);
    }
    return img;
}

bool writePpm(const fs::path& path, const Image& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << img.w << ' ' << img.h << "\n255\n";
    f.write(reinterpret_cast<const char*>(img.rgb.data()), std::streamsize(img.rgb.size()));
    return bool(f);
}

bool readPpm(const fs::path& path, Image& img) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int maxv = 0;
    if (!(f >> magic >> img.w >> img.h >> maxv) || magic != "P6" || maxv != 255) return false;
    f.get();  // single whitespace after the header
    img.rgb.resize(size_t(img.w) * size_t(img.h) * 3);
    f.read(reinterpret_cast<char*>(img.rgb.data()), std::streamsize(img.rgb.size()));
    return bool(f);
}

// Run on a fresh settings file with the defaults: the HUD and menus show
// setting values, and other runs/tests write assets/settings.cfg. The
// player's file is never touched.
void useFreshSettingsFile() {
    const fs::path settingsFile = fs::temp_directory_path() / "wexyron_render_capture_settings.cfg";
    fs::remove(settingsFile);
    const std::string settingsEnv = "CUBE_SETTINGS_PATH=" + settingsFile.string();
    static std::string keepAlive;  // putenv keeps a pointer to the string
    keepAlive = settingsEnv;
    putenv(keepAlive.data());
}

// `setup` runs before every frame: render() ends with glfwPollEvents(), and the
// game window captures the mouse, so a moving mouse would turn the camera
// between the two frames.
void renderAndSave(Game& game, const fs::path& out, const std::string& name, int& failures,
                   const std::function<void()>& setup) {
    // Two frames: the first may still upload/rebuild state lazily. Some
    // visuals read the wall clock (idle animation, bomb-site pulse), so pin it.
    setup();
    glfwSetTime(1.0);
    game.render();
    setup();
    glfwSetTime(1.0);
    const Image img = renderAndCapture(game);
    if (std::getenv("CAPTURE_VERBOSE")) {
        const Camera& c = game.debugCamera();
        std::printf("  camera %-24s pos=(%.3f %.3f %.3f) yaw=%.2f pitch=%.2f fov=%.2f\n", name.c_str(),
                    c.Position.x, c.Position.y, c.Position.z, c.Yaw, c.Pitch, c.Zoom);
    }
    bool blank = true;
    for (size_t i = 3; i < img.rgb.size() && blank; ++i) blank = img.rgb[i] == img.rgb[i % 3];
    if (img.w <= 0 || blank || !writePpm(out / (name + ".ppm"), img)) {
        std::printf("FAIL %-28s (empty frame or write error)\n", name.c_str());
        ++failures;
        return;
    }
    std::printf("captured %-28s %dx%d\n", name.c_str(), img.w, img.h);
}

int capture(const fs::path& out) {
    fs::create_directories(out);
    int failures = 0;

    useFreshSettingsFile();

    Game game(800, 600);
    game.init();
    if (const IRenderBackend* backend = game.debugRenderer().backend()) {
        const RenderBackendInfo info = backend->describe();
        std::printf("backend: %s %s on %s\n", info.api.c_str(), info.version.c_str(), info.renderer.c_str());
    }

    const char* maps[] = {"simple.map", "arena.map", "large.map", "sniper.map",
                          "complex.map", "team_arena.map", "custom.map"};
    for (const char* map : maps) {
        if (!game.debugStartMapForTest(map)) {
            std::printf("FAIL %s could not start\n", map);
            ++failures;
            continue;
        }
        // Bots (fixed RNG seeds) put skinned glTF bodies into the frame.
        game.debugStartLocalBotsForTest(map);
        // Fixed simulation time so HUD clocks, bots and the weapon pose match.
        game.debugStepForTest(30, 1.0f / 60.0f);
        const glm::vec3 dims = game.debugCurrentMap()->getDimensions();
        const std::string stem = fs::path(map).stem().string();

        // Maps are centred on the origin. Overview from high above one corner
        // looking across the map (yaw 45 = towards +x/+z), then eye height
        // from the centre.
        const glm::vec3 overviewEye(-dims.x * 0.45f, std::max(4.0f, dims.y * 0.8f), -dims.z * 0.45f);
        renderAndSave(game, out, stem + "_overview", failures,
                      [&] { game.debugSetViewForTest(overviewEye, 45.0f, -25.0f); });
        renderAndSave(game, out, stem + "_center", failures,
                      [&] { game.debugSetViewForTest(glm::vec3(1.5f, 1.7f, 1.5f), 45.0f, -5.0f); });
    }

    for (const char* menu : {"main", "settings", "framegen", "maps"}) {
        renderAndSave(game, out, std::string("menu_") + menu, failures,
                      [&] { game.debugOpenMenu(menu); });
    }

    // Resizing: the renderer must follow the framebuffer size on the next
    // frame (there is no resize callback; render() reads the size itself).
    glfwSetWindowSize(game.windowHandle(), 1024, 640);
    for (int i = 0; i < 3; ++i) {
        glfwPollEvents();
        game.render();
    }
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(game.windowHandle(), &fbW, &fbH);
    const Image resized = renderAndCapture(game);
    if (resized.w != fbW || resized.h != fbH || fbW != 1024) {
        std::printf("FAIL resize: frame %dx%d, framebuffer %dx%d\n", resized.w, resized.h, fbW, fbH);
        ++failures;
    } else {
        std::printf("resize ok: frame follows framebuffer %dx%d\n", fbW, fbH);
    }

    // Vsync off and on again: Vulkan recreates its swapchain for this.
    for (bool vsync : {false, true}) {
        game.debugRenderer().setVsync(vsync);
        for (int i = 0; i < 3; ++i) game.render();
        const Image img = renderAndCapture(game);
        if (img.w != fbW || img.h != fbH) {
            std::printf("FAIL vsync %s: no frame after the change\n", vsync ? "on" : "off");
            ++failures;
        }
    }
    std::printf("vsync switch ok\n");

    game.cleanup();
    std::printf(failures ? "CAPTURE: %d FAILURES\n" : "CAPTURE: ALL OK\n", failures);
    return failures ? 1 : 0;
}

int compare(const fs::path& ref, const fs::path& out, double maxMean) {
    int failures = 0, count = 0;
    for (const auto& entry : fs::directory_iterator(ref)) {
        if (entry.path().extension() != ".ppm") continue;
        ++count;
        const std::string name = entry.path().filename().string();
        Image a, b;
        if (!readPpm(entry.path(), a) || !readPpm(out / name, b)) {
            std::printf("FAIL %-32s missing or unreadable\n", name.c_str());
            ++failures;
            continue;
        }
        if (a.w != b.w || a.h != b.h) {
            std::printf("FAIL %-32s size %dx%d vs %dx%d\n", name.c_str(), a.w, a.h, b.w, b.h);
            ++failures;
            continue;
        }
        double sum = 0.0;
        size_t bad = 0;
        for (size_t p = 0; p < a.rgb.size(); p += 3) {
            int worst = 0;
            for (int c = 0; c < 3; ++c) {
                const int d = std::abs(int(a.rgb[p + c]) - int(b.rgb[p + c]));
                sum += d;
                worst = std::max(worst, d);
            }
            if (worst > 8) ++bad;
        }
        const double mean = sum / double(a.rgb.size());
        const double badPct = 100.0 * double(bad) / double(a.rgb.size() / 3);
        const bool ok = mean <= maxMean;
        std::printf("%s %-32s mean=%.3f differing=%.3f%%\n", ok ? "ok  " : "FAIL", name.c_str(), mean, badPct);
        if (!ok) ++failures;
    }
    if (count == 0) {
        std::printf("FAIL no reference images in %s\n", ref.string().c_str());
        return 1;
    }
    std::printf(failures ? "COMPARE: %d of %d FAILED\n" : "COMPARE: %d of %d OK\n",
                failures ? failures : count, count);
    return failures ? 1 : 0;
}

} // namespace

// Average frame time with vsync off: arena overview with bots, 600 frames.
int bench() {
    useFreshSettingsFile();
    Game game(800, 600);
    game.init();
    const RenderBackendInfo info = game.debugRenderer().backend()->describe();
    if (!game.debugStartMapForTest("arena.map")) return 1;
    game.debugStartLocalBotsForTest("arena.map");
    game.debugStepForTest(30, 1.0f / 60.0f);
    const glm::vec3 dims = game.debugCurrentMap()->getDimensions();
    const glm::vec3 eye(-dims.x * 0.45f, std::max(4.0f, dims.y * 0.8f), -dims.z * 0.45f);
    game.debugRenderer().setVsync(false);
    auto frame = [&] {
        game.debugSetViewForTest(eye, 45.0f, -25.0f);
        game.render();
    };
    for (int i = 0; i < 60; ++i) frame();   // warm-up
    const int frames = 600;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) frame();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("bench %s on %s: %.3f ms/frame (%.0f fps) over %d frames\n", info.api.c_str(),
                info.renderer.c_str(), ms / frames, 1000.0 * frames / ms, frames);
    game.cleanup();
    return 0;
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "capture" && argc > 2) return capture(argv[2]);
    if (mode == "bench") return bench();
    if (mode == "compare" && argc > 3) return compare(argv[2], argv[3], argc > 4 ? std::atof(argv[4]) : 0.5);
    std::printf("usage: render_capture_test capture <outdir>\n"
                "       render_capture_test compare <refdir> <outdir> [maxMeanDiff]\n"
                "       render_capture_test bench\n");
    return 2;
}
