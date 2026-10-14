// ============================================================
//  ADCAlignment — measures the dispersion actually present in a frame
//
//  ADCAssistant predicts how much atmospheric dispersion there SHOULD
//  be at a given altitude. That is an advisor: it tells you whether a
//  corrector is worth fitting. It cannot tell you whether the one you
//  fitted is dialled in, because it never looks at the image.
//
//  This looks at the image. It measures the red-to-blue displacement
//  actually present, which is what is left after the corrector has done
//  whatever it is currently doing. Dial the prisms, watch the number
//  fall, stop when it stops falling.
//
//  Deliberately free of Windows and D2D so it can be driven by
//  tests/adc_alignment.cpp with synthetic frames whose answer is known
//  by construction.
// ============================================================

#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

namespace raduk::core {

struct DispersionMeasurement {
    bool  valid       = false;

    // Blue centroid minus red centroid. Screen convention: +x right,
    // +y DOWN, because that is how the frame buffer is laid out.
    float dxPx        = 0.0f;
    float dyPx        = 0.0f;

    float magPx       = 0.0f;
    float magArcsec   = 0.0f;

    // Compass-style, 0 = up the frame (toward the top edge), increasing
    // clockwise. Up is where zenith is in an unrotated alt-az view, and
    // dispersion runs along the zenith line, so a correct reading sits
    // near 0 or near 180.
    float angleDeg    = 0.0f;

    // How many pixels either channel counts as target. Reported so a
    // reading taken off twelve pixels of a faint target cannot be
    // mistaken for one taken off a planet.
    int   signalPx    = 0;
};

class ADCAlignment {
public:
    // One frame, as the camera delivered it: the RAW BAYER buffer, not
    // the debayered preview.
    //
    // Raw for two reasons. The preview buffer has display FX and the
    // RGB Align shift applied, so measuring it would partly be
    // measuring our own correction. And the debayer is a 2x2 superpixel
    // one that takes red from a block's top-left and blue from its
    // bottom-right, baking in a one-pixel diagonal offset between the
    // channels that no corrector can remove -- measured at 1.70 px of
    // phantom residual on the emulator's dispersion-free Jupiter.
    // Reading the raw frame exposes the sampling grid, so that offset
    // is a known constant to subtract rather than an error to live
    // with.
    //
    // Works by finding the offset that best overlays blue on red, NOT
    // by differencing their centroids. A centroid assumes the two
    // images differ only by a displacement, which on the targets this
    // rig points at is false: Jupiter's and Saturn's belts are
    // red-brown, so red's first moment sits elsewhere on the disc than
    // blue's for reasons that have nothing to do with the atmosphere.
    // On the emulator's banded Jupiter, which has no dispersion at all,
    // the centroid difference read 0.63 arcsec and drifted as the
    // planet turned. Matching the whole structure is immune to that:
    // the belts are in both channels in the same places, at different
    // contrast, and the best overlap is still the true displacement.
    static DispersionMeasurement MeasureBayer(const uint8_t* raw, int w, int h,
                                              int bitDepth,
                                              float plateScaleArcsecPx);

    // The matcher itself, on two single-channel planes of the same size.
    // pxPerSample says how many full-resolution pixels one sample spans,
    // so the answer comes back in full-resolution pixels whatever
    // resolution the planes are at. Exposed so the tests can drive it
    // directly.
    static DispersionMeasurement MatchPlanes(const std::vector<float>& red,
                                             const std::vector<float>& blue,
                                             int pw, int ph,
                                             float pxPerSample,
                                             float plateScaleArcsecPx);

    // Rolling history. A single frame's centroid difference at ~0.2
    // arcsec/px is dominated by seeing -- consecutive frames disagree by
    // more than the residual being hunted. Dialling a corrector against
    // a number that jumps is guesswork, so the window is what the tool
    // shows and the single frame is not.
    void Push(const DispersionMeasurement& m);
    void Reset();

    // Component-wise median over the window. Median rather than mean:
    // one frame ruined by a gust or a cosmic ray should not move the
    // number a corrector is being dialled against.
    DispersionMeasurement Smoothed(float plateScaleArcsecPx) const;

    int  SampleCount() const { return (int)m_history.size(); }

    // ~1.5 s at 30 fps, ~3 s at 15. Long enough to average seeing,
    // short enough that a prism nudge shows up while your hand is
    // still on it.
    static constexpr int WINDOW = 45;

    // Fraction of each channel's own span above background that a
    // pixel must clear to count as part of the target.
    //
    // Relative to the channel's OWN span, deliberately. Red and blue
    // differ in brightness through any filter and at any white balance,
    // and an absolute threshold would find a different target in each
    // channel.
    static constexpr float THRESHOLD_FRAC = 0.50f;

    // How far, in full-resolution pixels, blue is allowed to have moved
    // from red. 16 px is about 3.2 arcsec at this rig's plate scale,
    // comfortably past the worst dispersion the model predicts at any
    // altitude worth imaging from. A match pinned to the edge of this
    // range is reported invalid rather than clamped, because a clamp is
    // not a measurement.
    static constexpr int SEARCH_PX = 16;

    // The matching window is subsampled so its longest side is about
    // this many samples. The search runs per frame while the window is
    // open; at full resolution it would cost more than the rest of the
    // frame put together. Sub-pixel precision comes from the parabolic
    // fit, not from the sampling, so this costs search resolution and
    // not answer resolution.
    static constexpr int SUB_TARGET = 128;

    // Below this the reading is noise, not a measurement.
    static constexpr int MIN_SIGNAL_PX = 60;

private:
    std::vector<DispersionMeasurement> m_history;
};

} // namespace raduk::core
