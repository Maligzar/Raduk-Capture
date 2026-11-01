#pragma once
// ============================================================
// RoiFollower.h — Phase 107: keep the target on a small ROI by
// moving the ROI
//
// The fastest correction this rig has, because it has no gears in
// it. The mount still takes the slow part: the tracker is fed the
// target's position plus Offset(), so it keeps seeing the true error
// on the sensor and walks the window back towards home. Without that
// the target would always look centred, the mount would stop
// correcting, and the window would run off the edge of the sensor.
//
// A MOVE HAS TO BE SEEN TO LAND. The SDK says nothing about which
// frame the new origin applies from, so frames after a move can still
// be from the old window. Acting on those would move a second time for
// the same error. So nothing happens until the target has jumped by
// about what the window moved, which also measures the real latency.
// If that never happens the camera is not applying moves mid-capture:
// after kMaxUnconfirmed in a row the follower stops and says so.
//
// RoiFollower is pure, with no Windows and no camera. RoiFollowLink
// adds the lock and the atomics for the two threads that share it.
//
// Phase 109: the follower only needs to know where the target is, so
// RoiFollowLink can answer from a disc's centroid or from SurfaceLock.
// It looks once when armed and stays with that for the lock.
// ============================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

#include "SurfaceLock.h"

namespace raduk::core {

// Where the bright thing is, cheaply enough to run on every frame on
// the capture thread. Every `stride`th pixel in each direction, weighted
// over the pixels above half the peak. An even stride reads one Bayer
// colour throughout, and RAW16 is left-aligned, so its high byte alone
// is the same scale as RAW8.
inline bool FastCentroid(const uint8_t* data, int width, int height,
                         int bitDepth, int stride,
                         float& outX, float& outY) {
    if (!data || width <= 0 || height <= 0 || stride < 1) return false;
    const int bytes = (bitDepth == 16) ? 2 : 1;
    const int high  = bytes - 1;
    const size_t rowBytes = (size_t)width * bytes;

    int peak = 0;
    for (int y = 0; y < height; y += stride) {
        const uint8_t* row = data + (size_t)y * rowBytes + high;
        for (int x = 0; x < width; x += stride) {
            const int v = row[(size_t)x * bytes];
            if (v > peak) peak = v;
        }
    }
    constexpr int kMinPeak = 32;   // below this it is noise, not a target
    if (peak < kMinPeak) return false;

    const int floorV = peak / 2;
    double sum = 0.0, sumX = 0.0, sumY = 0.0;
    int count = 0;
    for (int y = 0; y < height; y += stride) {
        const uint8_t* row = data + (size_t)y * rowBytes + high;
        for (int x = 0; x < width; x += stride) {
            const int v = row[(size_t)x * bytes];
            if (v <= floorV) continue;
            const double w = v - floorV;
            sum += w; sumX += w * x; sumY += w * y; ++count;
        }
    }
    if (count < 1 || sum <= 0.0) return false;
    outX = (float)(sumX / sum);
    outY = (float)(sumY / sum);
    return true;
}

class RoiFollower {
public:
    static constexpr int kStepX = 8;           // as the ROI placement aligns
    static constexpr int kStepY = 2;           // keeps the Bayer pattern
    static constexpr int kLandingTimeoutFrames = 60;
    static constexpr int kMaxUnconfirmed = 3;
    static constexpr int kJumpConfirmFrames = 3;

    struct Move { bool move = false; int x = 0, y = 0; };

    void Arm(int originX, int originY, int frameW, int frameH,
             int sensorW, int sensorH) {
        *this = RoiFollower{};
        m_armed = frameW > 0 && frameH > 0 &&
                  (frameW < sensorW || frameH < sensorH);
        m_x = m_homeX = originX;
        m_y = m_homeY = originY;
        m_w = frameW; m_h = frameH;
        m_maxX = ((std::max)(0, sensorW - frameW) / kStepX) * kStepX;
        m_maxY = ((std::max)(0, sensorH - frameH) / kStepY) * kStepY;
        m_deadband = (float)(std::max)(12, (std::min)(frameW, frameH) / 10);
        m_jumpPx   = (float)(std::max)(frameW, frameH) * 0.5f;
    }

    void Disarm() { m_armed = false; m_waiting = false; }

    Move Update(float cx, float cy, bool valid) {
        Move none;
        if (!m_armed) return none;

        if (m_waiting) {
            ++m_waited;
            if (valid &&
                Dist(cx, cy, m_expectX, m_expectY) < Dist(cx, cy, m_beforeX, m_beforeY)) {
                m_waiting = false;
                m_unconfirmed = 0;
                ++m_landed;
                m_lastLanding = m_waited;
                m_maxLanding = (std::max)(m_maxLanding, m_waited);
                m_lastX = cx; m_lastY = cy; m_haveLast = true;
            } else if (m_waited >= kLandingTimeoutFrames) {
                RefuseMove();
            }
            return none;
        }

        if (!valid) return none;              // hold; do not chase noise

        if (m_haveLast && Dist(cx, cy, m_lastX, m_lastY) > m_jumpPx) {
            // One odd frame must not throw the window across the sensor.
            // Three in agreement mean the target really is there.
            const bool agrees = m_jumpFrames > 0 &&
                                Dist(cx, cy, m_jumpX, m_jumpY) <= m_jumpPx;
            m_jumpFrames = agrees ? m_jumpFrames + 1 : 1;
            m_jumpX = cx; m_jumpY = cy;
            if (m_jumpFrames < kJumpConfirmFrames) return none;
        }
        m_jumpFrames = 0;
        m_lastX = cx; m_lastY = cy; m_haveLast = true;

        const float ex = cx - m_w * 0.5f, ey = cy - m_h * 0.5f;
        if (std::fabs(ex) <= m_deadband && std::fabs(ey) <= m_deadband)
            return none;

        const int nx = (std::clamp)(m_x + Quantise(ex, kStepX), 0, m_maxX);
        const int ny = (std::clamp)(m_y + Quantise(ey, kStepY), 0, m_maxY);
        if (nx == m_x && ny == m_y) return none;    // against the edge

        m_prevX = m_x; m_prevY = m_y;
        m_beforeX = cx; m_beforeY = cy;
        m_expectX = cx - (float)(nx - m_x);
        m_expectY = cy - (float)(ny - m_y);
        m_x = nx; m_y = ny;
        m_waiting = true; m_waited = 0;
        ++m_moves;
        none.move = true; none.x = nx; none.y = ny;
        return none;
    }

    // The move did not happen: the SDK refused it, or it never landed.
    void RefuseMove() {
        m_x = m_prevX; m_y = m_prevY;
        m_waiting = false;
        if (++m_unconfirmed >= kMaxUnconfirmed) {
            m_armed = false;
            m_gaveUp = true;
        }
    }

    bool Armed()     const { return m_armed; }
    bool GaveUp()    const { return m_gaveUp; }
    bool Waiting()   const { return m_waiting; }
    int  OriginX()   const { return m_x; }
    int  OriginY()   const { return m_y; }
    int  OffsetX()   const { return m_x - m_homeX; }
    int  OffsetY()   const { return m_y - m_homeY; }
    int  Moves()     const { return m_moves; }
    int  Landed()    const { return m_landed; }
    int  MaxLandingFrames() const { return m_maxLanding; }
    float DeadbandPx() const { return m_deadband; }

private:
    static float Dist(float ax, float ay, float bx, float by) {
        return std::hypot(ax - bx, ay - by);
    }
    static int Quantise(float e, int step) {
        return (int)std::lround(e / step) * step;
    }

    bool  m_armed = false, m_gaveUp = false, m_waiting = false, m_haveLast = false;
    int   m_x = 0, m_y = 0, m_homeX = 0, m_homeY = 0, m_prevX = 0, m_prevY = 0;
    int   m_w = 0, m_h = 0, m_maxX = 0, m_maxY = 0;
    float m_deadband = 0, m_jumpPx = 0;
    float m_expectX = 0, m_expectY = 0, m_beforeX = 0, m_beforeY = 0;
    float m_lastX = 0, m_lastY = 0, m_jumpX = 0, m_jumpY = 0;
    int   m_waited = 0, m_jumpFrames = 0, m_unconfirmed = 0;
    int   m_moves = 0, m_landed = 0, m_lastLanding = 0, m_maxLanding = 0;
};

// The follower between its two threads. OnFrame runs on the capture
// thread; everything else on the render loop. Disarmed, OnFrame is one
// relaxed load, so the path nobody asked for costs nothing.
class RoiFollowLink {
public:
    static constexpr int kStride = 4;

    enum class Mode : int { Unknown, Disc, Surface };

    // Runs on the capture thread, so the SDK is only ever called from
    // the thread that is already talking to it.
    void SetMover(std::function<bool(int, int)> mover) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_mover = std::move(mover);
    }

    void Arm(int originX, int originY, int frameW, int frameH,
             int sensorW, int sensorH) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_f.Arm(originX, originY, frameW, frameH, sensorW, sensorH);
        m_surface.Reset();
        m_mode.store((int)Mode::Unknown, std::memory_order_relaxed);
        Publish();
    }

    void Disarm() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_f.Disarm();
        Publish();
    }

    // Forget a give-up, so the button can try again.
    void Reset() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_f = RoiFollower{};
        Publish();
    }

    void OnFrame(const uint8_t* data, int width, int height, int bitDepth) {
        if (!m_armed.load(std::memory_order_relaxed)) return;
        if (CurrentMode() == Mode::Unknown) {
            const bool filled = FrameIsFilled(data, width, height, bitDepth, kStride);
            m_mode.store((int)(filled ? Mode::Surface : Mode::Disc),
                         std::memory_order_relaxed);
        }
        float cx = 0.f, cy = 0.f;
        bool valid = false;
        const bool surface = CurrentMode() == Mode::Surface;
        if (!surface)
            valid = FastCentroid(data, width, height, bitDepth, kStride, cx, cy);

        std::lock_guard<std::mutex> lk(m_mutex);
        if (surface)
            valid = m_surface.Measure(data, width, height, bitDepth, cx, cy);
        const int fromX = m_f.OriginX(), fromY = m_f.OriginY();
        const RoiFollower::Move m = m_f.Update(cx, cy, valid);
        if (m.move) {
            if (surface) m_surface.ExpectMove(m.x - fromX, m.y - fromY);
            if (!m_mover || !m_mover(m.x, m.y)) m_f.RefuseMove();
        }
        if (surface && !m_f.Waiting()) m_surface.ClearExpect();
        Publish();
    }

    bool Armed()   const { return m_armed.load(std::memory_order_relaxed); }
    int  OffsetX() const { return m_offX.load(std::memory_order_relaxed); }
    int  OffsetY() const { return m_offY.load(std::memory_order_relaxed); }
    int  OriginX() const { return m_orgX.load(std::memory_order_relaxed); }
    int  OriginY() const { return m_orgY.load(std::memory_order_relaxed); }
    bool GaveUp()  const { return m_gaveUp.load(std::memory_order_relaxed); }
    Mode CurrentMode() const { return (Mode)m_mode.load(std::memory_order_relaxed); }

    RoiFollower Snapshot() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_f;
    }

    int SurfaceLostFrames() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_surface.LostFrames();
    }
    int SurfaceRefreshes() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_surface.Refreshes();
    }

private:
    void Publish() {
        m_armed.store(m_f.Armed(), std::memory_order_relaxed);
        m_gaveUp.store(m_f.GaveUp(), std::memory_order_relaxed);
        m_offX.store(m_f.OffsetX(), std::memory_order_relaxed);
        m_offY.store(m_f.OffsetY(), std::memory_order_relaxed);
        m_orgX.store(m_f.OriginX(), std::memory_order_relaxed);
        m_orgY.store(m_f.OriginY(), std::memory_order_relaxed);
    }

    std::mutex        m_mutex;
    RoiFollower       m_f;
    SurfaceLock       m_surface;
    std::function<bool(int, int)> m_mover;
    std::atomic<int>  m_mode{0};
    std::atomic<bool> m_armed{false}, m_gaveUp{false};
    std::atomic<int>  m_offX{0}, m_offY{0}, m_orgX{0}, m_orgY{0};
};

} // namespace raduk::core
