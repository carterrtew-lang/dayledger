#pragma once
// The pomodoro rhythm on top of the ledger. Focus phases record to the chosen tag, and the record
// that reaches the end of the countdown is flagged as a completed pomodoro; breaks record to a
// "休息" tag (created on first use in the "other" category, in its colour family). Pure logic driven
// by explicit timestamps, tested like the model.

#include <algorithm>
#include <string>
#include "tracker_model.h"

namespace tracker {

struct PomodoroConfig {
    long long focus = 25 * 60, shortBreak = 5 * 60, longBreak = 15 * 60;  // seconds
    int longEvery = 4;          // a long break after this many completed focus phases
    bool autoNext = false;      // start the next focus phase by itself when a break ends
};

enum class PomodoroPhase { Ready, Focus, Break };

// What a step did, so the UI can play the right chime and show the right note.
struct PomodoroEvents {
    bool focusStarted = false, focusCompleted = false, breakStarted = false, breakEnded = false;
    bool interrupted = false, longBreak = false, restTagCreated = false;
    bool any() const {
        return focusStarted || focusCompleted || breakStarted || breakEnded || interrupted || restTagCreated;
    }
};

class Pomodoro {
public:
    PomodoroConfig config;
    PomodoroPhase phase = PomodoroPhase::Ready;
    bool paused = false;
    long long phaseEnd = 0;     // wall-clock end of the running phase
    long long remaining = 0;    // seconds left while paused
    int focusTag = 0;           // the tag focus phases record to; in Ready, the one offered next
    int restTag = 0;            // the tag breaks record to
    int cycle = 0;              // focus phases completed since the last long break
    bool longBreak = false;     // the current break is the long one

    inline static const std::wstring restName = L"休息";     // 休息
    static constexpr unsigned restColor = 0x00D65062u;               // #6250d6 (COLORREF)
    static constexpr long long awayAfter = 300;  // a gap between ticks this long: the computer was away

    bool running() const { return phase != PomodoroPhase::Ready && !paused; }

    long long length() const {
        if (phase == PomodoroPhase::Break) return longBreak ? config.longBreak : config.shortBreak;
        return config.focus;
    }

    long long remainingAt(long long now) const {
        if (phase == PomodoroPhase::Ready) return config.focus;
        if (paused) return remaining;
        return (std::max)(0LL, phaseEnd - now);
    }

    // Starts focusing on `tag`. During a focus it switches the tag and the countdown carries on;
    // during a break it ends the break early.
    PomodoroEvents focus(Model& m, int tag, long long now) {
        PomodoroEvents events;
        if (!m.findTag(tag)) return events;
        if (phase == PomodoroPhase::Focus && !paused) {
            if (m.activeTagId != tag) m.start(tag, now);
            focusTag = tag;
            return events;
        }
        if (phase == PomodoroPhase::Focus && paused) {
            focusTag = tag;
            m.start(tag, now);
            phaseEnd = now + remaining;
            paused = false;
            return events;
        }
        // Ready, or a break that ends early. A free-running record is split so that only the
        // focus phase ends up in the pomodoro record.
        if (m.activeTagId) m.stop(now);
        m.start(tag, now);
        phase = PomodoroPhase::Focus;
        paused = false;
        focusTag = tag;
        phaseEnd = now + config.focus;
        events.focusStarted = true;
        return events;
    }

    // Pausing stops the record (the time so far stays recorded) and freezes the countdown.
    void pause(Model& m, long long now) {
        if (!running()) return;
        remaining = remainingAt(now);
        if (m.activeTagId) m.stop(now);
        paused = true;
    }

    PomodoroEvents resume(Model& m, long long now) {
        PomodoroEvents events;
        if (phase == PomodoroPhase::Ready || !paused) return events;
        if (phase == PomodoroPhase::Focus && !m.findTag(focusTag)) {
            phase = PomodoroPhase::Ready;
            paused = false;
            return events;
        }
        const int tag = phase == PomodoroPhase::Focus ? focusTag : ensureRestTag(m, events);
        if (m.findTag(tag)) m.start(tag, now);
        phaseEnd = now + remaining;
        paused = false;
        return events;
    }

    // Focus: end it now (uncounted) and take a short break. Break: go straight to the next focus.
    // Ready: start focusing.
    PomodoroEvents skip(Model& m, long long now) {
        if (phase == PomodoroPhase::Focus) {
            if (m.activeTagId) m.stop(now);
            return startBreak(m, now, false);
        }
        if (phase == PomodoroPhase::Break) {
            if (m.activeTagId) m.stop(now);
            phase = PomodoroPhase::Ready;
            paused = false;
            auto events = focus(m, focusTag, now);
            events.breakEnded = true;
            return events;
        }
        return focus(m, focusTag, now);
    }

    // Gives up the current phase; recorded time stays, nothing is counted.
    void abandon(Model& m, long long now) {
        if (phase == PomodoroPhase::Ready) return;
        if (!paused && m.activeTagId) m.stop(now);
        phase = PomodoroPhase::Ready;
        paused = false;
    }

    // Leaving pomodoro mode: whatever is recording keeps recording as an ordinary timer.
    void leave() {
        phase = PomodoroPhase::Ready;
        paused = false;
    }

    // Advances the rhythm; `lastTick` is the previous tick, used to notice sleep and hibernation.
    PomodoroEvents tick(Model& m, long long now, long long lastTick) {
        PomodoroEvents events;
        if (!running()) return events;
        if (!m.activeTagId) {            // the record was stopped elsewhere: hold the countdown
            remaining = remainingAt(now);
            paused = true;
            return events;
        }
        if (phase == PomodoroPhase::Focus) focusTag = m.activeTagId;   // follows a retagged record
        if (phase == PomodoroPhase::Focus && lastTick > 0 && now - lastTick > awayAfter && lastTick < phaseEnd) {
            m.stop(lastTick);            // the focus did not happen while the computer was away
            phase = PomodoroPhase::Ready;
            paused = false;
            events.interrupted = true;
            return events;
        }
        if (now < phaseEnd) return events;
        if (phase == PomodoroPhase::Focus) {
            events = completeFocus(m, phaseEnd);
            if (now < phaseEnd) return events;
            const auto late = endBreak(m, now);       // a late tick: the break is over too
            events.breakEnded = late.breakEnded;
            events.focusStarted = late.focusStarted;
            return events;
        }
        return endBreak(m, now);
    }

private:
    int ensureRestTag(Model& m, PomodoroEvents& events) {
        if (restTag && m.findTag(restTag)) return restTag;
        for (const auto& tag : m.tags) {
            if (tag.name == restName) {
                restTag = tag.id;
                return restTag;
            }
        }
        if (m.categories.empty()) return 0;
        // Breaks go to the default "other" category (id 2) when it still exists, else the last one.
        const int category = m.findCategory(2) ? 2 : m.categories.back().id;
        restTag = m.addTag(restName, m.nextTagColor(category), category);
        events.restTagCreated = restTag != 0;
        return restTag;
    }

    PomodoroEvents startBreak(Model& m, long long at, bool isLong) {
        PomodoroEvents events;
        const int tag = ensureRestTag(m, events);
        if (m.activeTagId) m.stop(at);
        if (m.findTag(tag)) m.start(tag, at);
        phase = PomodoroPhase::Break;
        paused = false;
        longBreak = isLong;
        phaseEnd = at + (isLong ? config.longBreak : config.shortBreak);
        events.breakStarted = true;
        events.longBreak = isLong;
        return events;
    }

    PomodoroEvents completeFocus(Model& m, long long at) {
        if (m.activeTagId) m.stop(at, SessionPomodoro);
        ++cycle;
        const bool isLong = config.longEvery > 0 && cycle >= config.longEvery;
        if (isLong) cycle = 0;
        auto events = startBreak(m, at, isLong);
        events.focusCompleted = true;
        return events;
    }

    PomodoroEvents endBreak(Model& m, long long now) {
        PomodoroEvents events;
        const long long end = phaseEnd;
        if (m.activeTagId) m.stop(end);
        phase = PomodoroPhase::Ready;
        paused = false;
        events.breakEnded = true;
        if (config.autoNext && now - end < 60 && m.findTag(focusTag))
            events.focusStarted = focus(m, focusTag, end).focusStarted;
        return events;
    }
};

} // namespace tracker
