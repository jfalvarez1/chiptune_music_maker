#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <numbers> // C++20

namespace DSP {

using Complex = std::complex<float>;
using Spectrum = std::vector<Complex>;

const float PI = 3.14159265358979323846f;

// Simple Cooley-Tukey FFT (Recursive)
// Optimization: Could be iterative with bit-reversal lookup for speed, 
// but for this tool recursive is likely fast enough for offline analysis.
inline void fft(std::vector<Complex>& x) {
    const size_t N = x.size();
    if (N <= 1) return;

    // Divide
    std::vector<Complex> even(N / 2);
    std::vector<Complex> odd(N / 2);
    for (size_t i = 0; i < N / 2; ++i) {
        even[i] = x[i * 2];
        odd[i] = x[i * 2 + 1];
    }

    // Conquer
    fft(even);
    fft(odd);

    // Combine
    for (size_t k = 0; k < N / 2; ++k) {
        Complex t = std::polar(1.0f, -2.0f * PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}

// Inverse FFT
inline void ifft(std::vector<Complex>& x) {
    // Conjugate the complex numbers
    for (auto& val : x) val = std::conj(val);
    
    // Forward FFT
    fft(x);
    
    // Conjugate again and scale
    for (auto& val : x) {
        val = std::conj(val);
        val /= static_cast<float>(x.size());
    }
}

// ============================================================================
// Reusable iterative FFT plan
//
// The recursive fft() above allocates two vectors at every level of the
// recursion - about 2N allocations for an N-point transform. That is fine
// for a one-shot offline analysis, which is what it was written for, and
// unusable for real-time tracking: a 1024-point transform every 5.3 ms works
// out at roughly 400,000 allocations a second, which stalls frames even
// though none of it is on the audio thread.
//
// This is the standard iterative Cooley-Tukey: bit-reversal permutation,
// then log2(N) butterfly passes, in place. The bit-reversal indices and the
// twiddle factors are computed once when the size is set, so a transform
// allocates nothing at all.
// ============================================================================
class FFTPlan {
public:
    // N must be a power of two. Returns false and leaves the plan unusable
    // otherwise, rather than half-transforming and producing a spectrum that
    // looks plausible and is wrong.
    bool resize(size_t n) {
        if (n < 2 || (n & (n - 1)) != 0) return false;
        if (n == m_size) return true;

        m_size = n;

        m_reversed.resize(n);
        size_t bits = 0;
        while ((size_t(1) << bits) < n) ++bits;
        for (size_t i = 0; i < n; ++i) {
            size_t reversed = 0;
            for (size_t b = 0; b < bits; ++b) {
                if (i & (size_t(1) << b)) reversed |= size_t(1) << (bits - 1 - b);
            }
            m_reversed[i] = reversed;
        }

        // One twiddle per half-size, shared by every stage that needs it.
        m_cos.resize(n / 2);
        m_sin.resize(n / 2);
        for (size_t i = 0; i < n / 2; ++i) {
            const float angle = -2.0f * PI * static_cast<float>(i) /
                                static_cast<float>(n);
            m_cos[i] = std::cos(angle);
            m_sin[i] = std::sin(angle);
        }
        return true;
    }

    size_t size() const { return m_size; }

    // In-place forward transform of a split real/imaginary pair. Both spans
    // must hold size() elements.
    void transform(float* real, float* imag) const {
        const size_t n = m_size;
        if (n < 2) return;

        for (size_t i = 0; i < n; ++i) {
            const size_t j = m_reversed[i];
            if (j > i) {
                std::swap(real[i], real[j]);
                std::swap(imag[i], imag[j]);
            }
        }

        for (size_t len = 2; len <= n; len <<= 1) {
            const size_t half = len >> 1;
            const size_t step = n / len;
            for (size_t start = 0; start < n; start += len) {
                for (size_t k = 0; k < half; ++k) {
                    const size_t twiddle = k * step;
                    const float wr = m_cos[twiddle];
                    const float wi = m_sin[twiddle];

                    const size_t a = start + k;
                    const size_t b = a + half;

                    const float tr = real[b] * wr - imag[b] * wi;
                    const float ti = real[b] * wi + imag[b] * wr;

                    real[b] = real[a] - tr;
                    imag[b] = imag[a] - ti;
                    real[a] += tr;
                    imag[a] += ti;
                }
            }
        }
    }

private:
    size_t m_size = 0;
    std::vector<size_t> m_reversed;
    std::vector<float> m_cos, m_sin;
};

// Helper to get magnitude spectrum from real audio chunk
inline std::vector<float> computeMagnitudeSpectrum(const std::vector<float>& samples) {
    size_t N = samples.size();
    // Ensure power of 2
    size_t p2 = 1;
    while (p2 < N) p2 <<= 1;
    
    std::vector<Complex> buffer(p2, 0.0f);
    for(size_t i=0; i<N; ++i) buffer[i] = samples[i]; // Copy with zero padding if needed

    fft(buffer);

    std::vector<float> magnitudes(p2 / 2); // Only first half (Nyquist)
    for(size_t i=0; i<p2/2; ++i) {
        magnitudes[i] = std::abs(buffer[i]);
    }
    return magnitudes;
}

}
