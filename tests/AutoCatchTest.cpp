// Offline auto-catch tests.
//
// Each scenario holds the pedal (or taps it), plays notes built from a
// brass-like harmonic series, then goes silent. We then check that every
// played pitch keeps sounding at roughly the level it was played, that no
// extra layers were caught, and that releasing the pedal fades to silence.
//
// Build with -DSOSTENAHTO_BUILD_TESTS=ON, then run
// build/AutoCatchTest_artefacts/Release/AutoCatchTest. Set ACT_DEBUG=1 to print each onset.

#include <JuceHeader.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "PluginProcessor.h"

extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kNoteLevel = 0.2;                // amplitude of each note's fundamental
constexpr double kExpectedLevel = kNoteLevel / 2; // what goertzel() reports for it

struct Note { double freq, start, length; const char* name; };

struct Scenario {
    const char* name = "";
    std::vector<Note> notes;
    bool autoCatch = true;
    bool latch = false;
    bool vibrato = false;
    std::vector<double> taps; // empty: hold the pedal from holdFrom. Otherwise tap at each time, then hold.
    double holdFrom = 0.2;
    std::vector<std::pair<double, double>> holds; // if set: pedal down only during these (start, end) spans
    std::vector<Note> absent; // notes that were played but must NOT be sounding while measuring
    int expectedLayers = -1;  // most layers active at once (default: number of notes)
    double total = 4.5, measureFrom = 3.0, measureTo = 4.3;
};

float synth(double t, const Scenario& sc, juce::Random& rng)
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    float out = 0.f;
    for (const auto& n : sc.notes) {
        double local = t - n.start;
        if (local < 0.0 || local > n.length) continue;
        // Tongued attack: a short noisy burst, then a steady tone with a soft release.
        double env = std::min(1.0, local / 0.02) * std::min(1.0, (n.length - local) / 0.03);
        // Optional 5.5 Hz vibrato: +/- 0.6% pitch plus a little loudness wobble.
        double phase = twoPi * n.freq * local;
        if (sc.vibrato) phase -= n.freq * 0.006 / 5.5 * std::cos(twoPi * 5.5 * local);
        double tone = 0.0;
        for (int k = 1; k <= 8; k++) tone += std::sin(k * phase) / k;
        if (sc.vibrato) tone *= 1.0 + 0.08 * std::sin(twoPi * 5.5 * local);
        double attackNoise = local < 0.03 ? (rng.nextFloat() * 2.f - 1.f) * (1.0 - local / 0.03) * 0.5 : 0.0;
        out += (float)(kNoteLevel * env * (tone + attackNoise));
    }
    return out;
}

double goertzel(const std::vector<float>& x, double freq)
{
    double w = juce::MathConstants<double>::twoPi * freq / kSampleRate;
    double coeff = 2.0 * std::cos(w), s1 = 0.0, s2 = 0.0;
    for (float v : x) {
        double s = v + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / (double)x.size();
}

bool pedalWanted(const Scenario& sc, double t)
{
    if (!sc.holds.empty()) {
        for (const auto& h : sc.holds)
            if (t >= h.first && t < h.second) return true;
        return false;
    }
    if (sc.taps.empty()) return t >= sc.holdFrom;
    for (double tap : sc.taps)
        if (t >= tap && t < tap + 0.1) return true;
    return t >= sc.taps.back() + 0.25; // then hold (after the last note ends) so the layers keep ringing
}

bool run(const Scenario& sc)
{
    std::printf("\n== %s\n", sc.name);
    std::unique_ptr<juce::AudioProcessor> base(createPluginFilter());
    auto* proc = dynamic_cast<SustainPedalAudioProcessor*>(base.get());
    if (proc == nullptr) { std::printf("FAIL: wrong processor type\n"); return false; }

    base->setPlayConfigDetails(2, 2, kSampleRate, kBlock);
    base->prepareToPlay(kSampleRate, kBlock);
    proc->start();       // equivalent to skipping the donate screen
    proc->setDry(0.f);   // listen to the sustain only
    proc->setWet(100.f);
    proc->setAutoCatch(sc.autoCatch);
    proc->setLatch(sc.latch);

    juce::Random rng(1234);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> measured;
    int maxLayersSeen = 0;
    size_t lastTail = 0;
    const bool debug = std::getenv("ACT_DEBUG") != nullptr;

    const int nBlocks = (int)(sc.total * kSampleRate / kBlock);
    for (int b = 0; b < nBlocks; b++) {
        const double t0 = b * kBlock / kSampleRate;

        bool want = pedalWanted(sc, t0);
        if (want != proc->isPedalDown()) proc->setPedal(want);

        for (int i = 0; i < kBlock; i++) {
            float s = synth(t0 + i / kSampleRate, sc, rng);
            buffer.setSample(0, i, s);
            buffer.setSample(1, i, s);
        }
        base->processBlock(buffer, midi);
        maxLayersSeen = std::max(maxLayersSeen, proc->getActiveLayerCount());

        size_t tailNow = proc->getLatestRecording().size();
        if (debug && tailNow < lastTail)
            std::printf("  onset at %.3fs (pedal %d, layers %d)\n", t0, (int)proc->isPedalDown(), proc->getActiveLayerCount());
        lastTail = tailNow;

        if (t0 >= sc.measureFrom && t0 < sc.measureTo)
            for (int i = 0; i < kBlock; i++) measured.push_back(buffer.getSample(0, i));

        // Give the background sample-preparation threads time to run, as in real time.
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }

    bool ok = true;
    const int expectedLayers = sc.expectedLayers >= 0 ? sc.expectedLayers : (int)sc.notes.size();
    std::printf("most layers at once: %d (expected %d)\n", maxLayersSeen, expectedLayers);
    if (maxLayersSeen != expectedLayers) ok = false;

    // Background reference: a whole tone above the first note, which is never played.
    double reference = goertzel(measured, sc.notes[0].freq * 1.1225);
    for (const auto& n : sc.notes) {
        bool cleared = false;
        for (const auto& a : sc.absent) cleared |= (a.freq == n.freq && a.start == n.start);
        if (cleared) continue;
        // The frozen pitch can sit slightly off the nominal note (e.g. caught mid-vibrato),
        // so take the strongest reading within +/-1%.
        double level = 0.0;
        for (double f = n.freq * 0.99; f <= n.freq * 1.01; f += n.freq * 0.0005)
            level = std::max(level, goertzel(measured, f));
        std::printf("  %-4s sustains at %.2fx its played level (%.0fx background)\n",
                    n.name, level / kExpectedLevel, level / (reference + 1e-9));
        if (level < 0.4 * kExpectedLevel || level > 1.6 * kExpectedLevel) ok = false;
    }
    for (const auto& n : sc.absent) {
        double level = 0.0;
        for (double f = n.freq * 0.99; f <= n.freq * 1.01; f += n.freq * 0.0005)
            level = std::max(level, goertzel(measured, f));
        std::printf("  %-4s should be cleared: %.2fx its played level\n", n.name, level / kExpectedLevel);
        if (level > 0.05 * kExpectedLevel) ok = false;
    }

    // Piano mode: releasing the pedal fades everything out.
    // Latch mode: releasing keeps it ringing, so tap the pedal to clear instead.
    auto silentBlock = [&] { buffer.clear(); base->processBlock(buffer, midi); };
    if (sc.latch) {
        proc->setPedal(true);
        for (int b = 0; b < 4; b++) silentBlock();
    }
    proc->setPedal(false);
    float tailPeak = 0.f;
    for (int b = 0; b < (int)(2.5 * kSampleRate / kBlock); b++) {
        silentBlock();
        if (b * kBlock / kSampleRate > 2.0) tailPeak = std::max(tailPeak, buffer.getMagnitude(0, 0, kBlock));
    }
    std::printf("level 2 s after %s: %.6f\n", sc.latch ? "a clearing tap" : "release", tailPeak);
    if (tailPeak > 1e-4f) ok = false;

    std::printf(ok ? "PASS\n" : "FAIL\n");
    base->releaseResources();
    return ok;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::vector<Scenario> scenarios;
    {
        Scenario sc;
        sc.name = "Hold pedal, tongue C3 E3 G3";
        sc.notes = { { 130.81, 0.40, 0.55, "C3" }, { 164.81, 1.00, 0.55, "E3" }, { 196.00, 1.60, 0.55, "G3" } };
        scenarios.push_back(sc);
    }
    {
        Scenario sc;
        sc.name = "Same notes with vibrato (no extra layers allowed)";
        sc.notes = { { 130.81, 0.40, 0.55, "C3" }, { 164.81, 1.00, 0.55, "E3" }, { 196.00, 1.60, 0.55, "G3" } };
        sc.vibrato = true;
        scenarios.push_back(sc);
    }
    {
        Scenario sc;
        sc.name = "Slurred (overlapping) Bb2 D3 F3";
        sc.notes = { { 116.54, 0.40, 0.62, "Bb2" }, { 146.83, 1.00, 0.62, "D3" }, { 174.61, 1.60, 0.62, "F3" } };
        scenarios.push_back(sc);
    }
    {
        Scenario sc;
        sc.name = "Low register: Bb1 then F2";
        sc.notes = { { 58.27, 0.40, 0.8, "Bb1" }, { 87.31, 1.30, 0.8, "F2" } };
        scenarios.push_back(sc);
    }
    {
        Scenario sc;
        sc.name = "Auto catch off: tap the pedal during each note";
        sc.notes = { { 130.81, 0.40, 0.55, "C3" }, { 164.81, 1.00, 0.55, "E3" } };
        sc.autoCatch = false;
        sc.taps = { 0.75, 1.35 };
        scenarios.push_back(sc);
    }

    {
        Scenario sc;
        sc.name = "Latch: press, play C3 E3 G3, release, keeps ringing";
        sc.notes = { { 130.81, 0.40, 0.55, "C3" }, { 164.81, 1.00, 0.55, "E3" }, { 196.00, 1.60, 0.55, "G3" } };
        sc.latch = true;
        sc.holds = { { 0.2, 2.25 } }; // let go right after the last note; measured from 3.0 s with the pedal up
        scenarios.push_back(sc);
    }
    {
        Scenario sc;
        sc.name = "Latch: a new press clears the old chord and catches the new one";
        sc.absent = { { 130.81, 0.40, 0.55, "C3" }, { 164.81, 1.00, 0.55, "E3" } };
        sc.notes = { { 196.00, 2.40, 0.55, "G3" } };
        for (const auto& n : sc.absent) sc.notes.insert(sc.notes.begin(), n); // play C3, E3 first
        sc.latch = true;
        sc.expectedLayers = 2; // C3 + E3, then just G3
        sc.holds = { { 0.2, 1.7 }, { 2.2, 3.05 } };
        sc.total = 6.0; sc.measureFrom = 4.5; sc.measureTo = 5.8;
        scenarios.push_back(sc);
    }

    int failed = 0;
    for (const auto& sc : scenarios)
        if (!run(sc)) failed++;

    std::printf("\n%s (%d of %zu scenarios failed)\n", failed == 0 ? "ALL PASSED" : "SOME FAILED", failed, scenarios.size());
    return failed == 0 ? 0 : 1;
}
