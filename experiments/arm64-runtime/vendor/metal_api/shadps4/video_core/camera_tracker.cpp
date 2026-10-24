// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/camera_tracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bbport_toggles.h"

namespace VideoCore {

std::array<float, 12> Multiply(const std::array<float, 12>& a, const std::array<float, 12>& b) {
    std::array<float, 12> out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            float v = c == 3 ? a[r * 4 + 3] : 0.0f;
            for (int k = 0; k < 3; ++k) {
                v += a[r * 4 + k] * b[k * 4 + c];
            }
            out[r * 4 + c] = v;
        }
    }
    return out;
}

std::array<float, 12> InverseAffine(const std::array<float, 12>& m) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9],
                i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1e-12f) {
        return m;
    }
    const float s = 1.0f / det;
    const std::array<float, 9> r = {(e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s,
                                    (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s,
                                    (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s};
    std::array<float, 12> out{};
    for (int row = 0; row < 3; ++row) {
        out[row * 4 + 0] = r[row * 3 + 0];
        out[row * 4 + 1] = r[row * 3 + 1];
        out[row * 4 + 2] = r[row * 3 + 2];
        out[row * 4 + 3] = -(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
    }
    return out;
}

float CameraTracker::VerticalFov() const noexcept {
    return 2.0f * std::atan(1.0f / std::abs(current.proj[1]));
}

float CameraTracker::Near() const noexcept {
    // depth = zs + zo / z is 0 at the near plane.
    return -current.proj[3] / current.proj[2];
}

void CameraTracker::PrintState(int frame) const {
    const auto print = [](const char* name, const float* v, int n) {
        std::printf(" %s", name);
        for (int i = 0; i < n; ++i) {
            std::printf(" %.9g", v[i]);
        }
    };
    std::printf("Dump camera: frame %d", frame);
    print("view", current.view.data(), 12);
    print("prev_view", previous.view.data(), 12);
    print("proj", current.proj.data(), 4);
    print("prev_proj", previous.proj.data(), 4);
    print("jitter", jitter.data(), 2);
    print("prev_jitter", previous_jitter.data(), 2);
    std::printf("\n");
}

std::array<std::array<float, 4>, 3> CameraTracker::TaaDepthParameters() const noexcept {
    const auto transform = Multiply(previous.view, current.inv_view);
    return {current.proj, previous.proj,
            {transform[8], transform[9], transform[10], transform[11]}};
}

MotionConstants CameraTracker::Constants(u32 width, u32 height, u32 mode) const {
    return {
        .reproject = Multiply(previous.view, current.inv_view),
        .proj = current.proj,
        .prev_proj = previous.proj,
        .size = {float(width), float(height)},
        .jitter = jitter,
        .previous_jitter = previous_jitter,
        .mode = mode,
    };
}

void CameraTracker::OnConstants(const float* data) {
    // Scene constants: far plane 3000, 1/far, and the render size.
    if (data[0] != 3000.0f || data[4] < 64.0f || data[5] < 64.0f ||
        std::abs(data[1] * data[0] - 1.0f) > 1e-3f) {
        return;
    }
    // bbport: the first constants of each G-buffer pass are its camera. A frame may have more
    // than one such pass (another camera before the main scene): the camera goes with the pass
    // whose depth the motion vectors use (CommitFrameCamera), not with the frame's first pass.
    if (pass_has_camera) {
        return;
    }
    std::memcpy(pass_camera.view.data(), data + 8, 12 * sizeof(float));
    std::memcpy(pass_camera.inv_view.data(), data + 180, 12 * sizeof(float));
    // bbport: BB_CAMERA_INVERSE=own: the inverse of the view matrix itself instead of the one the
    // game stores beside it (A/B; they agree to float precision). BB_CAMERA_LOG prints the gap.
    static const bool own_inverse = [] {
        const char* env = std::getenv("BB_CAMERA_INVERSE");
        return env && std::strcmp(env, "own") == 0;
    }();
    const auto computed_inverse = InverseAffine(pass_camera.view);
    float inverse_gap = 0.0f;
    for (int i = 0; i < 12; ++i) {
        inverse_gap = std::max(inverse_gap, std::abs(computed_inverse[i] - pass_camera.inv_view[i]));
    }
    const auto game_inverse = pass_camera.inv_view;
    if (own_inverse != BbToggle::Disabled(BbToggle::CameraOwnInverse)) {
        pass_camera.inv_view = computed_inverse;
    }
    // bbport: BB_CAMERA_LOG=1 prints the camera position every 100 ms (scripted tests of how far
    // the player moves in a given time at different frame rates).
    static const bool camera_log = std::getenv("BB_CAMERA_LOG") != nullptr;
    if (camera_log) {
        using Clock = std::chrono::steady_clock;
        static const auto start = Clock::now();
        static auto next = start;
        if (const auto now = Clock::now(); now >= next) {
            next = now + std::chrono::milliseconds(100);
            std::printf("Camera: %.3f s at %.3f %.3f %.3f (inverse of view: %.3f %.3f %.3f, largest "
                        "difference %.4f)\n",
                        std::chrono::duration<double>(now - start).count(), game_inverse[3],
                        game_inverse[7], game_inverse[11], computed_inverse[3], computed_inverse[7],
                        computed_inverse[11], inverse_gap);
        }
    }
    // bbport: the projection as the motion shaders use it: ndc +y down the screen. 0.2 assumed the
    // viewport flips y (ndc +y up, as at Yahar'gul: yscale -540), 0.3 that it does not; the
    // viewport of the frame's G-buffer pass decides (BB_CAMERA_Y=up/down forces one, for tests).
    static const float forced_y = [] {
        const char* env = std::getenv("BB_CAMERA_Y");
        return !env ? 0.0f : std::strcmp(env, "up") == 0 ? -1.0f : std::strcmp(env, "down") == 0 ? 1.0f : 0.0f;
    }();
    const float y_sign = (forced_y != 0.0f ? forced_y : gbuffer_y_sign) *
                         (BbToggle::Disabled(BbToggle::CameraYFlip) ? -1.0f : 1.0f);
    pass_camera.proj = {data[52] * gbuffer_x_sign, data[57] * y_sign,
                    data[62], data[63]};
    {
        // bbport: the projection's y scale sign, printed when it changes (see the G-buffer
        // viewport line): which way view +y goes on the screen.
        static int last_sign = 0;
        const int sign = (data[57] < 0.0f ? -2 : 2) + (gbuffer_y_sign < 0.0f ? -1 : 1);
        if (sign != last_sign) {
            last_sign = sign;
            std::printf("Camera motion: projection x %.4f y %.4f z %.6f %.6f, G-buffer viewport y "
                        "%s: screen y follows view %s\n",
                        data[52], data[57], data[62], data[63],
                        gbuffer_y_sign < 0.0f ? "flipped" : "not flipped",
                        pass_camera.proj[1] < 0.0f ? "-y" : "+y");
        }
    }
    pass_camera.valid = pass_camera.proj[0] != 0.0f && pass_camera.proj[1] != 0.0f;
    const std::array<u32, 2> size{u32(data[4]), u32(data[5])};
    if (size != render_size) {
        std::printf("Camera motion: scene render size %ux%u\n", size[0], size[1]);
        render_size = size;
    }
    pass_has_camera = true;
    frame_has_camera = true;
    if (frame_gbuffer_passes <= pass_positions.size() && frame_gbuffer_passes > 0) {
        // World position of this pass's camera (diagnostics: frames with several passes).
        const auto& v = pass_camera.view;
        pass_positions[frame_gbuffer_passes - 1] = {
            -(v[0] * v[3] + v[4] * v[7] + v[8] * v[11]),
            -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]),
            -(v[2] * v[3] + v[6] * v[7] + v[10] * v[11])};
    }
}

void CameraTracker::OnGBufferPass(ImageId depth, float x_sign, float y_sign) {
    if (depth != depth_id) {
        // Another depth target: another G-buffer pass, with its own camera.
        depth_id = depth;
        pass_has_camera = false;
        ++frame_gbuffer_passes;
    }
    gbuffer_x_sign = x_sign;
    gbuffer_y_sign = y_sign;
}

void CameraTracker::CommitFrameCamera() {
    if (committed || !pass_has_camera) {
        return;
    }
    previous = current;
    current = pass_camera;
    committed = true;
    if (current.valid) {
        // bbport: the camera's world position for the frame stats (route checks on any backend).
        for (int i = 0; i < 3; ++i) {
            std::uint32_t bits;
            std::memcpy(&bits, &current.inv_view[i * 4 + 3], sizeof(bits));
            BbStats::camera_position_bits[i].store(bits, std::memory_order_relaxed);
        }
        BbStats::camera_frames.fetch_add(1, std::memory_order_release);
    }
}

bool CameraTracker::OnDisplayPass() {
    CommitFrameCamera();
    const bool ready = depth_id && current.valid && previous.valid;
    if (frame_gbuffer_passes > 1) {
        static auto next = std::chrono::steady_clock::now();
        if (const auto now = std::chrono::steady_clock::now(); now >= next) {
            next = now + std::chrono::seconds(2);
            std::printf("Camera motion: %u G-buffer passes this frame, cameras at", frame_gbuffer_passes);
            for (u32 i = 0; i < std::min<u32>(frame_gbuffer_passes, pass_positions.size()); ++i) {
                std::printf(" (%.2f %.2f %.2f)", pass_positions[i][0], pass_positions[i][1],
                            pass_positions[i][2]);
            }
            std::printf("; the last pass's camera goes with its depth\n");
        }
    }
    if (!frame_has_camera) InvalidateHistory();
    frame_has_camera = false;
    pass_has_camera = false;
    committed = false;
    frame_gbuffer_passes = 0;
    depth_id = {};
    return ready;
}

} // namespace VideoCore
