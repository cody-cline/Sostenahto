/*
  ==============================================================================

    TransientDetect.h
    Created: 19 Apr 2023 11:04:46am
    Author:  andre

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <pffft.hpp>

class TransientDetector
{
public:
    TransientDetector(int sampleRate, int bufferSize, double frameTime = 0.08, double overlap = 0.75, double trackingTime = 2);
	~TransientDetector() {}
    // Feeds audio in; clears tailBuffer at each detected onset.
    // Returns true if an onset was detected during this call.
    bool process(std::vector<float> buffer, std::vector<float>& tailBuffer);

	void setSampleRate(int val) { sampleRate = val; setup(); }
	void setBufferSize(int val) { bufferSize = val; setup(); }
	void setOverlap(double val) { overlap = val; setup(); }
	void setFrameTime(double val) { frameTime = val; setup(); }
	void setTrackingTime(double val) { trackingTime = val; setup(); }

#if JUCE_DEBUG
	std::vector<float> getFrameFlags() { return frameFlags; }
#endif

private:
	void setup();
	bool detectTransient(std::vector<float> input);
	void applyWindow(std::vector<float>& buf);
	int sgn(float val);

	std::vector<std::vector<std::complex<float>>> fftHistory;
	std::vector<std::vector<float>> magStore, phaseStore, fftFunction;
	std::vector<float> inputStream, frameFlags, windowFunction, sumFunctions;
	std::vector<bool> transientFrames;
	bool processing = false;
	std::unique_ptr<std::future<bool>> isTransient;

	int sampleRate, bufferSize, windowSize, fftSize, nTrackedFrames, stepSize;

	// Onset detection tuning (normalised spectral flux).
	std::vector<std::vector<float>> magHistory;
	int fluxLag = 2;               // compare with the frame this many hops back
	int fluxSpread = 2;            // +/- bins of tolerance for pitch wobble
	float onsetThreshold = 0.3f;   // fraction of the spectrum that must be new
	float onsetFloor = 0.003f;     // RMS below this (about -50 dBFS) is treated as silence
	int lockoutFrames = 4, framesSinceOnset = 1000;
	double overlap, frameTime, trackingTime;
};