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
