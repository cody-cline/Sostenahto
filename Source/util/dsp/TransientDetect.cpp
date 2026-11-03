/*
  ==============================================================================

    TransientDetect.cpp
    Created: 19 Apr 2023 11:04:46am
    Author:  andre

  ==============================================================================
*/

#include "TransientDetect.h"

TransientDetector::TransientDetector(int sampleRate, int bufferSize, double frameTime, double overlap, double trackingTime) :
    sampleRate(sampleRate), bufferSize(bufferSize), frameTime(frameTime), overlap(overlap), trackingTime(trackingTime) {
    setup();
}

void TransientDetector::setup() {
    windowSize = 1;
    int timeToSamples = sampleRate * frameTime;
    for (; timeToSamples > 0; timeToSamples /= 2, windowSize *= 2) {}
    if (windowSize < bufferSize) windowSize = bufferSize;
    fftSize = windowSize / 2;

    stepSize = (1.0 - overlap) * windowSize;
    nTrackedFrames = trackingTime * sampleRate / stepSize;

    // About 40 ms between onsets, so one attack only registers once or twice.
    lockoutFrames = std::max(2, (int)(0.04 * sampleRate / stepSize));
    magHistory.clear();

    windowFunction = std::vector<float>(windowSize, 0.f);
    sumFunctions = std::vector<float>(fftSize, 0.f);
    dsp::WindowingFunction<float>::fillWindowingTables(&windowFunction[0], windowSize, dsp::WindowingFunction<float>::blackmanHarris);
}

bool TransientDetector::process(std::vector<float> buffer, std::vector<float>& tailBuffer) {
    for (int i = 0; i < buffer.size(); i++) {
        inputStream.push_back(buffer[i]);
    }
    bool found = false;
    // Process every complete analysis window (blocks larger than the hop size
    // would otherwise make the input stream grow without bound).
    while (inputStream.size() >= windowSize) {
        bool transient = detectTransient(std::vector<float>(inputStream.begin(), inputStream.begin() + windowSize));
        if (transient) {
            tailBuffer.clear();
            found = true;
        }
        inputStream.erase(inputStream.begin(), inputStream.begin() + stepSize);
    }
    return found;
}

bool TransientDetector::detectTransient(std::vector<float> input) {
    auto fft = pffft::Fft<float>(windowSize);
    auto in = fft.valueVector();
    auto out = fft.spectrumVector();
    double energy = 0.0;
    for (int i = 0; i < windowSize; i++) {
        in[i] = input[i] * windowFunction[i];
        energy += (double)input[i] * input[i];
    }
    fft.forward(in, out);

    std::vector<float> mags(out.size());
    for (size_t i = 0; i < out.size(); i++) {
        mags[i] = std::abs(out[i]);
    }

    // Normalised positive spectral flux: how much of this frame's spectrum is
    // "new" compared with the previous frame (0 = steady tone, 1 = all new).
    // A steady note stays near 0 even with vibrato, while a tongued attack or a
    // change of pitch brings in new harmonics and jumps well above the threshold.
    //
    // Like "SuperFlux", each bin is compared against the loudest nearby bin of
    // an earlier frame, so small pitch wobbles (vibrato, or the spectrum of a
    // low note flickering between frames) are not mistaken for new notes.
    float flux = 0.f;
    if (magHistory.size() >= (size_t)fluxLag) {
        const auto& ref = magHistory[magHistory.size() - (size_t)fluxLag];
        float rise = 0.f, total = 1e-9f;
        const int n = (int)mags.size();
        for (int i = 0; i < n; i++) {
            float refMax = 0.f;
            for (int k = std::max(0, i - fluxSpread); k <= std::min(n - 1, i + fluxSpread); k++)
                refMax = std::max(refMax, ref[(size_t)k]);
            rise += std::max(0.f, mags[(size_t)i] - refMax);
            total += mags[(size_t)i];
        }
        flux = rise / total;
    }
    magHistory.push_back(mags);
    if (magHistory.size() > (size_t)fluxLag) magHistory.erase(magHistory.begin());

    const float rms = (float)std::sqrt(energy / windowSize);
    framesSinceOnset++;

#if JUCE_DEBUG
    frameFlags.push_back(flux * 100.f);
    if (frameFlags.size() > (size_t)nTrackedFrames) frameFlags.erase(frameFlags.begin());
#endif

    // Ignore near-silence (breath, room noise) and re-triggers within the lockout.
    if (rms < onsetFloor || framesSinceOnset < lockoutFrames) return false;
    if (flux < onsetThreshold) return false;

    framesSinceOnset = 0;
    return true;
}

void TransientDetector::applyWindow(std::vector<float>& buf) {
    for (int i = 0; i < windowSize; i++) {
        buf[i] *= windowFunction[i];
    }
}

int TransientDetector::sgn(float val) {
    return val >= 0 ? 1 : -1;
}