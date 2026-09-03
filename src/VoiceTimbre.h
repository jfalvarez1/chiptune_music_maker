#pragma once

// ============================================================================
// Telling one mouth sound from another
//
// Classifying beatboxed percussion - is that a kick, a snare, or a hi-hat -
// from a short window of audio starting at the onset.
//
// WHY THE OLD APPROACH WAS NOT ENOUGH. The first version used spectral
// centroid, zero-crossing rate and two band-energy ratios with hand-tuned
// thresholds. That family of features is measured in the literature at
// roughly 67% on this task. It separates a kick from everything else
// perfectly well - almost all of a kick's energy is under 250 Hz - and then
// struggles exactly where it matters, between a snare and a hi-hat, because
// a snare is half noise and lands in hi-hat territory on both centroid and
// zero-crossing rate.
//
// WHAT REPLACES IT. Fourteen MFCCs plus four envelope descriptors, matched
// with k-nearest-neighbours at k=3. The same combination is measured at
// about 84% on five-way phoneme separability, and the reason it works is
// that MFCCs describe the *shape* of the spectrum rather than one summary
// number of it, while the envelope descriptors capture what the spectrum
// cannot: a snare and an open hi-hat are long, a kick and a closed hi-hat
// are short, and that difference is in the amplitude envelope only.
//
// WHAT THE USER SHOULD BE TOLD TO DO. The research finding that matters
// most here is counterintuitive: a fixed, prescribed set of mouth sounds
// classified generically scores far better (~97%) than letting people use
// whatever sounds they like and training per-user (~79%). The phonemes that
// separate best are /p/ for kick, /k/ for snare, and /ts/ and /tS/ for the
// hi-hats. /t/ is the one most people reach for and the worst performer -
// it is scattered across the /ts/ and /tS/ regions and cannot be pulled
// apart from them reliably.
//
// So the UI's job is to teach those four sounds, and this classifier's job
// is to learn them from a handful of examples when somebody insists on
// their own.
//
// Nothing here runs on the audio thread. Feature extraction is called from
// the same UI-thread poll that drains the capture ring.
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// The feature vector
// ============================================================================

inline constexpr int MFCC_COUNT = 14;      // the count the literature settles on
inline constexpr int ENVELOPE_COUNT = 4;
inline constexpr int TIMBRE_FEATURES = MFCC_COUNT + ENVELOPE_COUNT;

struct TimbreFeatures {
    std::array<float, TIMBRE_FEATURES> values{};

    bool finite() const {
        for (float value : values) {
            if (!std::isfinite(value)) return false;
        }
        return true;
    }
};

/*
 * Mel from Hz, and back.
 *
 * The mel scale spaces bands the way hearing does - closely at the bottom,
 * widely at the top - which is why a mel filterbank describes a percussive
 * sound better than linear bins do. A kick's whole character lives in a
 * couple of hundred Hz that linear bins would lump into one.
 */
inline float hzToMel(float hz) {
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

inline float melToHz(float mel) {
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

/*
 * Mel-frequency cepstral coefficients from a magnitude spectrum.
 *
 * Triangular mel filters, log energy, then a DCT-II. The DCT is what turns
 * a 26-band spectrum into a handful of numbers describing its overall
 * shape: the first coefficient is loudness, the next few are the broad
 * tilt, and the rest are the finer structure that distinguishes a "k" from
 * a "ts".
 *
 * Coefficient zero is deliberately kept. It is loudness, which for speech
 * recognition is a nuisance to be discarded - but here a hard hit and a
 * soft one are genuinely different events, and the classifier is allowed to
 * use that.
 */
inline void computeMFCC(const float* magnitudeSpectrum, int bins,
                        int sampleRate, float* out, int count) {
    if (magnitudeSpectrum == nullptr || out == nullptr || bins <= 0) return;

    constexpr int FILTER_COUNT = 26;   // the standard bank size
    const float nyquist = float(sampleRate) * 0.5f;

    // Filter edges, evenly spaced on the mel scale.
    float edges[FILTER_COUNT + 2];
    const float melLow = hzToMel(40.0f);        // below a kick's fundamental
    const float melHigh = hzToMel(std::min(nyquist, 16000.0f));
    for (int i = 0; i < FILTER_COUNT + 2; ++i) {
        const float mel = melLow + (melHigh - melLow) *
                                       float(i) / float(FILTER_COUNT + 1);
        edges[i] = melToHz(mel);
    }

    const float binHz = nyquist / float(bins);

    float logEnergy[FILTER_COUNT];
    for (int f = 0; f < FILTER_COUNT; ++f) {
        const float left = edges[f];
        const float centre = edges[f + 1];
        const float right = edges[f + 2];

        float energy = 0.0f;
        for (int bin = 0; bin < bins; ++bin) {
            const float hz = float(bin) * binHz;
            if (hz <= left || hz >= right) continue;

            // Triangular weight: rising to the centre, falling after it.
            const float weight = (hz <= centre)
                ? (hz - left) / std::max(1e-6f, centre - left)
                : (right - hz) / std::max(1e-6f, right - centre);
            energy += magnitudeSpectrum[bin] * weight;
        }

        // Log, floored. A silent band is not minus infinity, and one such
        // band would otherwise poison every coefficient through the DCT.
        logEnergy[f] = std::log(std::max(energy, 1e-10f));
    }

    // DCT-II, orthonormalised so the coefficients are comparable in scale.
    for (int c = 0; c < count; ++c) {
        float sum = 0.0f;
        for (int f = 0; f < FILTER_COUNT; ++f) {
            sum += logEnergy[f] *
                   std::cos(3.14159265358979f * float(c) *
                            (float(f) + 0.5f) / float(FILTER_COUNT));
        }
        out[c] = sum * std::sqrt(2.0f / float(FILTER_COUNT));
    }
}

/*
 * Four numbers describing the shape of the amplitude envelope.
 *
 * These are what the spectrum cannot tell you. A snare and an open hi-hat
 * ring; a kick and a closed hi-hat stop. Two sounds can have almost the
 * same spectrum and be obviously different to a listener purely because one
 * decays in 40 ms and the other in 300 ms.
 *
 *   0  the slope after the peak    - how fast it dies away
 *   1  the steepest rise before it - how sharp the attack is
 *   2  flatness                    - sustained, or one spike
 *   3  temporal centroid ratio     - where its weight sits, 0..1
 */
inline void computeEnvelopeDescriptors(const float* samples, int count,
                                       float* out) {
    if (samples == nullptr || out == nullptr || count <= 1) {
        if (out != nullptr) for (int i = 0; i < ENVELOPE_COUNT; ++i) out[i] = 0.0f;
        return;
    }

    // A rectified, smoothed envelope. The raw waveform oscillates through
    // zero, so its derivative says nothing about the shape of the sound.
    std::vector<float> envelope(static_cast<size_t>(count), 0.0f);
    float follower = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float rectified = std::fabs(samples[i]);
        // Fast up, slow down - an envelope follower, so the attack is not
        // smeared but the decay is smooth enough to differentiate.
        follower = (rectified > follower) ? rectified
                                          : follower * 0.995f + rectified * 0.005f;
        envelope[static_cast<size_t>(i)] = follower;
    }

    int peakIndex = 0;
    float peak = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (envelope[static_cast<size_t>(i)] > peak) {
            peak = envelope[static_cast<size_t>(i)];
            peakIndex = i;
        }
    }
    if (peak <= 1e-9f) {
        for (int i = 0; i < ENVELOPE_COUNT; ++i) out[i] = 0.0f;
        return;
    }

    // 0: mean derivative after the peak, weighted by amplitude, normalised
    // by the peak so it describes shape rather than loudness.
    float afterSum = 0.0f;
    float afterWeight = 0.0f;
    for (int i = peakIndex + 1; i < count; ++i) {
        const float derivative = envelope[static_cast<size_t>(i)] -
                                 envelope[static_cast<size_t>(i - 1)];
        const float weight = envelope[static_cast<size_t>(i)];
        afterSum += derivative * weight;
        afterWeight += weight;
    }
    out[0] = (afterWeight > 1e-9f) ? (afterSum / afterWeight) / peak : 0.0f;

    // 1: the steepest single rise before the peak.
    float steepest = 0.0f;
    for (int i = 1; i <= peakIndex; ++i) {
        const float derivative = envelope[static_cast<size_t>(i)] -
                                 envelope[static_cast<size_t>(i - 1)];
        steepest = std::max(steepest, derivative);
    }
    out[1] = steepest / peak;

    // 2: flatness - the mean over the peak. Near 1 is a sustained sound,
    // near 0 is a single spike with nothing around it.
    float total = 0.0f;
    for (float value : envelope) total += value;
    out[2] = (total / float(count)) / peak;

    // 3: where the sound's weight sits along its own length. An open hi-hat
    // carries its weight late; a closed one is all at the front.
    float weighted = 0.0f;
    float mass = 0.0f;
    for (int i = 0; i < count; ++i) {
        weighted += float(i) * envelope[static_cast<size_t>(i)];
        mass += envelope[static_cast<size_t>(i)];
    }
    out[3] = (mass > 1e-9f) ? (weighted / mass) / float(count) : 0.0f;
}

/*
 * The whole feature vector for one hit.
 *
 * The window should start at the onset and run about 46 ms - the length
 * measured as best for this, long enough to contain the sound's character
 * and short enough not to reach the next hit.
 */
inline TimbreFeatures extractTimbre(const float* samples, int count,
                                    const float* magnitudeSpectrum, int bins,
                                    int sampleRate) {
    TimbreFeatures features;
    computeMFCC(magnitudeSpectrum, bins, sampleRate,
                features.values.data(), MFCC_COUNT);
    computeEnvelopeDescriptors(samples, count,
                               features.values.data() + MFCC_COUNT);

    // A NaN anywhere would silently make every distance NaN and every
    // comparison false, so the classifier would answer the same thing
    // forever. Better a zeroed feature than a poisoned one.
    for (float& value : features.values) {
        if (!std::isfinite(value)) value = 0.0f;
    }
    return features;
}

// ============================================================================
// The classifier
// ============================================================================

enum class DrumClass : uint8_t {
    Kick = 0,
    Snare,
    HatClosed,
    HatOpen,
    Count
};

inline const char* drumClassName(DrumClass value) {
    switch (value) {
        case DrumClass::Kick:      return "Kick";
        case DrumClass::Snare:     return "Snare";
        case DrumClass::HatClosed: return "Closed hat";
        case DrumClass::HatOpen:   return "Open hat";
        default:                   return "?";
    }
}

/*
 * The mouth sound to teach for each drum.
 *
 * Not a cosmetic label. These four separate better than any other set
 * measured, and a user who follows them gets dramatically better results
 * than one who picks their own - which is the opposite of what one expects,
 * and the reason the UI should say so rather than leaving people to guess.
 *
 * /t/ is deliberately absent. It is the sound most people reach for and the
 * worst performer of the lot: it scatters across the /ts/ and /tS/ regions
 * and cannot be reliably pulled apart from either.
 */
inline const char* drumClassPhoneme(DrumClass value) {
    switch (value) {
        case DrumClass::Kick:      return "\"puh\"";
        case DrumClass::Snare:     return "\"kuh\"";
        case DrumClass::HatClosed: return "\"tss\"";
        case DrumClass::HatOpen:   return "\"tshh\"";
        default:                   return "";
    }
}

struct TimbreExample {
    TimbreFeatures features;
    DrumClass label = DrumClass::Kick;
};

/*
 * k-nearest-neighbours over taught examples.
 *
 * k=3 throughout, which is the value the published sweeps settle on for
 * this task, and which is also the smallest k that lets a single bad
 * example be outvoted.
 *
 * Fixed capacity, because the whole point is a handful of examples per
 * class - somebody recording thousands would be better served by a
 * different design, and an unbounded vector here would make the per-hit
 * cost grow without anybody noticing.
 */
class DrumClassifier {
public:
    static constexpr int K = 3;
    static constexpr int MAX_EXAMPLES = 256;
    static constexpr int MIN_PER_CLASS = 3;

    void clear() {
        m_examples.clear();
        m_normalised = false;
    }

    bool addExample(const TimbreFeatures& features, DrumClass label) {
        if (!features.finite()) return false;
        if (m_examples.size() >= MAX_EXAMPLES) return false;
        m_examples.push_back(TimbreExample{features, label});
        m_normalised = false;
        return true;
    }

    int exampleCount() const { return static_cast<int>(m_examples.size()); }

    int countFor(DrumClass label) const {
        int total = 0;
        for (const TimbreExample& example : m_examples) {
            if (example.label == label) ++total;
        }
        return total;
    }

    // Which classes have been taught enough times to be worth asking about.
    int taughtClasses() const {
        int total = 0;
        for (int c = 0; c < int(DrumClass::Count); ++c) {
            if (countFor(static_cast<DrumClass>(c)) >= MIN_PER_CLASS) ++total;
        }
        return total;
    }

    /*
     * Usable only once at least two classes have enough examples.
     *
     * One class is not a classifier - it would answer "kick" to everything,
     * confidently. Falling back to the built-in heuristic until then is
     * better than appearing to work.
     */
    bool trained() const { return taughtClasses() >= 2; }

    /*
     * Per-feature normalisation.
     *
     * MFCC zero is loudness and swings over tens; the envelope descriptors
     * live in fractions of one. Without normalising, Euclidean distance is
     * decided almost entirely by the loudest coefficient and the other
     * seventeen features may as well not be there.
     */
    void computeNormalisation() const {
        for (int f = 0; f < TIMBRE_FEATURES; ++f) {
            float mean = 0.0f;
            for (const TimbreExample& example : m_examples) {
                mean += example.features.values[static_cast<size_t>(f)];
            }
            mean /= float(std::max<size_t>(1, m_examples.size()));

            float variance = 0.0f;
            for (const TimbreExample& example : m_examples) {
                const float d = example.features.values[static_cast<size_t>(f)] - mean;
                variance += d * d;
            }
            variance /= float(std::max<size_t>(1, m_examples.size()));

            m_mean[static_cast<size_t>(f)] = mean;
            // A feature that never varies contributes nothing; a zero here
            // would divide by it.
            m_scale[static_cast<size_t>(f)] =
                std::sqrt(variance) > 1e-6f ? 1.0f / std::sqrt(variance) : 0.0f;
        }
        m_normalised = true;
    }

    struct Result {
        DrumClass label = DrumClass::Kick;
        float confidence = 0.0f;   // share of the k neighbours that agreed
        bool valid = false;
    };

    Result classify(const TimbreFeatures& features) const {
        return classifyAmong(features, true, true, true, true);
    }

    /*
     * Classify among only some of the classes.
     *
     * This is where the accuracy of a narrowed kit comes from, and it has
     * to be done HERE rather than by classifying four ways and discarding
     * unwanted answers. Discarding throws away real hits; restricting the
     * vote cannot produce an answer that needs discarding, and removes the
     * confusions between classes that are not in play at all - which for
     * vocal percussion is most of the error, since snare against hi-hat is
     * where the mistakes live.
     *
     * The neighbours are still searched over every taught example: an
     * example of a class that is switched off is simply not counted. A "tss"
     * taught as a hat still informs the geometry even when hats are off,
     * because it tells the normalisation what the spread of the features
     * is.
     */
    Result classifyAmong(const TimbreFeatures& features, bool allowKick,
                         bool allowSnare, bool allowHatClosed,
                         bool allowHatOpen) const {
        Result result;
        if (!trained() || !features.finite()) return result;
        if (!m_normalised) computeNormalisation();

        const bool allowed[int(DrumClass::Count)] = {
            allowKick, allowSnare, allowHatClosed, allowHatOpen};

        bool any = false;
        for (bool value : allowed) any = any || value;
        if (!any) return result;

        // The k nearest, kept as a tiny insertion-sorted list rather than by
        // sorting every example - k is 3 and the example count is small.
        float bestDistance[K];
        int bestIndex[K];
        for (int i = 0; i < K; ++i) {
            bestDistance[i] = std::numeric_limits<float>::max();
            bestIndex[i] = -1;
        }

        for (size_t e = 0; e < m_examples.size(); ++e) {
            if (!allowed[int(m_examples[e].label)]) continue;

            float distance = 0.0f;
            for (int f = 0; f < TIMBRE_FEATURES; ++f) {
                const float scale = m_scale[static_cast<size_t>(f)];
                if (scale <= 0.0f) continue;
                const float a = (features.values[static_cast<size_t>(f)] -
                                 m_mean[static_cast<size_t>(f)]) * scale;
                const float b = (m_examples[e].features.values[static_cast<size_t>(f)] -
                                 m_mean[static_cast<size_t>(f)]) * scale;
                const float d = a - b;
                distance += d * d;
            }

            for (int slot = 0; slot < K; ++slot) {
                if (distance >= bestDistance[slot]) continue;
                for (int shift = K - 1; shift > slot; --shift) {
                    bestDistance[shift] = bestDistance[shift - 1];
                    bestIndex[shift] = bestIndex[shift - 1];
                }
                bestDistance[slot] = distance;
                bestIndex[slot] = static_cast<int>(e);
                break;
            }
        }

        int votes[int(DrumClass::Count)] = {};
        int counted = 0;
        for (int i = 0; i < K; ++i) {
            if (bestIndex[i] < 0) continue;
            ++votes[int(m_examples[static_cast<size_t>(bestIndex[i])].label)];
            ++counted;
        }
        if (counted == 0) return result;

        int winner = 0;
        for (int c = 1; c < int(DrumClass::Count); ++c) {
            if (votes[c] > votes[winner]) winner = c;
        }

        result.label = static_cast<DrumClass>(winner);
        result.confidence = float(votes[winner]) / float(counted);
        result.valid = true;
        return result;
    }

    // ---- Persistence ---------------------------------------------------------
    //
    // Taught sounds belong to the person, not the project - they are the
    // same whichever song is open - so they live beside the settings rather
    // than in the .ctp.
    bool save(const std::string& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << "# ChiptuneTracker taught vocal percussion\n";
        file << "# Safe to delete - it just means teaching them again.\n";
        for (const TimbreExample& example : m_examples) {
            file << int(example.label);
            for (float value : example.features.values) file << ' ' << value;
            file << '\n';
        }
        return file.good();
    }

    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        m_examples.clear();
        m_normalised = false;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream stream(line);

            int label = -1;
            if (!(stream >> label)) continue;
            if (label < 0 || label >= int(DrumClass::Count)) continue;

            TimbreExample example;
            example.label = static_cast<DrumClass>(label);
            bool complete = true;
            for (int f = 0; f < TIMBRE_FEATURES; ++f) {
                if (!(stream >> example.features.values[static_cast<size_t>(f)])) {
                    // A short line is from another version. Skipped rather
                    // than half-read, which would be a garbage example that
                    // quietly drags every classification toward it.
                    complete = false;
                    break;
                }
            }
            if (!complete || !example.features.finite()) continue;
            if (m_examples.size() >= MAX_EXAMPLES) break;
            m_examples.push_back(example);
        }
        return true;
    }

    const std::vector<TimbreExample>& examples() const { return m_examples; }

    // Forget the most recent example of a class, for an undo button.
    bool removeLast(DrumClass label) {
        for (size_t i = m_examples.size(); i > 0; --i) {
            if (m_examples[i - 1].label != label) continue;
            m_examples.erase(m_examples.begin() + long(i - 1));
            m_normalised = false;
            return true;
        }
        return false;
    }

private:
    std::vector<TimbreExample> m_examples;

    // Mutable so classify() can stay const - the normalisation is a cache of
    // the examples, not state of its own.
    mutable std::array<float, TIMBRE_FEATURES> m_mean{};
    mutable std::array<float, TIMBRE_FEATURES> m_scale{};
    mutable bool m_normalised = false;
};

inline const char* DRUM_TRAINING_FILENAME = "chiptune-vocal-drums.ini";

inline std::string drumTrainingPath(const std::string& directory = std::string()) {
    if (directory.empty()) return DRUM_TRAINING_FILENAME;
    return directory + "/" + DRUM_TRAINING_FILENAME;
}

} // namespace ChiptuneTracker
