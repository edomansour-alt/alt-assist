#pragma once
// SteerEstimator.hpp - hold/release balance steering for Spam Assist.
// Header-only with no Geode dependency, so the logic can be tested outside the game.
//
// Idea: a wave goes up while held and down while released, at equal speed. So the
// net slope of any spam is set by the fraction of each click cycle you spend holding
// (the "duty"). Duty 50% = straight, 75% = half-slope up, 25% = half-slope down,
// 100% = full 45 degrees up, 0% = full 45 degrees down.
//   slope = 2 * duty - 1        (tan of the angle for a normal-size wave)
#include <algorithm>
#include <cmath>
#include <deque>

namespace spam {

struct SteerConfig {
    float deadzone  = 0.10f; // duty within 0.5 +/- deadzone counts as even spam -> straight
    float strength  = 1.0f;  // 1.0 = same slope as your own spam, higher = steeper
    int   window    = 6;     // how many recent click cycles to average
    float maxSlope  = 0.9f;  // 1.0 would be a full 45 degree line (just holding or releasing)
    float smoothing = 0.5f;  // 0 = react instantly, closer to 1 = slower and calmer
    double maxPeriod = 0.3;  // a click cycle longer than this (s) isn't spam: forget the history
};

class SteerEstimator {
public:
    explicit SteerEstimator(SteerConfig cfg = {}) : m_cfg(cfg) {}

    void setConfig(SteerConfig cfg) { m_cfg = cfg; }

    void reset() {
        m_cycles.clear();
        m_haveLastPress = false;
        m_haveRelease = false;
        m_duty = 0.5f;
        m_slope = 0.f;
        m_avgPeriod = 0.0;
    }

    // Call when the player's REAL input goes down. t = level time in seconds.
    void onPress(double t) {
        if (m_haveLastPress && m_haveRelease) {
            double period = t - m_lastPress;
            double hold = m_lastRelease - m_lastPress;
            if (period > m_cfg.maxPeriod) {
                // you stopped spamming (long hold or long pause): start neutral again
                m_cycles.clear();
                m_duty = 0.5f;
                m_slope = 0.f;
            } else if (period > 0.0 && hold >= 0.0 && hold <= period) {
                m_cycles.push_back({hold, period});
                while (static_cast<int>(m_cycles.size()) > std::max(1, m_cfg.window))
                    m_cycles.pop_front();
                recompute();
            }
        }
        m_lastPress = t;
        m_haveLastPress = true;
        m_haveRelease = false;
    }

    // Call when the player's REAL input goes up.
    void onRelease(double t) {
        if (m_haveLastPress && t >= m_lastPress) {
            m_lastRelease = t;
            m_haveRelease = true;
        }
    }

    float slope() const { return m_slope; }               // -1 down .. 0 straight .. +1 up
    float duty() const { return m_duty; }                 // 0..1 share of time spent holding
    double averagePeriod() const { return m_avgPeriod; }  // seconds per click; CPS = 1 / this

    // How long the assist should hold inside a cycle of `period` seconds to produce the
    // current slope. minTap keeps both the press and the gap at least one tick long.
    double holdFor(double period, double minTap) const {
        double hold = period * (0.5 + 0.5 * static_cast<double>(m_slope));
        double hi = std::max(minTap, period - minTap);
        return std::clamp(hold, minTap, hi);
    }

private:
    struct Cycle { double hold; double period; };

    void recompute() {
        double sumHold = 0.0, sumPeriod = 0.0;
        for (const auto& c : m_cycles) { sumHold += c.hold; sumPeriod += c.period; }
        if (sumPeriod <= 0.0) return;

        m_duty = static_cast<float>(sumHold / sumPeriod);
        m_avgPeriod = sumPeriod / static_cast<double>(m_cycles.size());

        float raw = (m_duty - 0.5f) * 2.f;  // -1..1, 0 = even spam
        float target = 0.f;
        if (std::fabs(raw) > m_cfg.deadzone * 2.f) {
            target = std::clamp(raw * m_cfg.strength, -m_cfg.maxSlope, m_cfg.maxSlope);
        }
        m_slope = m_slope * m_cfg.smoothing + target * (1.f - m_cfg.smoothing);
    }

    SteerConfig m_cfg;
    std::deque<Cycle> m_cycles;
    double m_lastPress = 0.0, m_lastRelease = 0.0;
    bool m_haveLastPress = false, m_haveRelease = false;
    float m_duty = 0.5f;
    float m_slope = 0.f;
    double m_avgPeriod = 0.0;
};

} // namespace spam
