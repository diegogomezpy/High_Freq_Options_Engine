#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <cmath>
#include <cassert>
#include <iomanip>
#include "math.hpp"

using namespace HFT::math;

// High-precision reference for verification
double reference_cdf(double x)
{
    return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

int main()
{
    constexpr size_t NUM_TESTS = 1'000'000;

    // 1. Setup deterministic random data
    std::mt19937 gen(42);
    std::uniform_real_distribution<double> dist(100.0, 200.0);

    struct Input
    {
        double S, K, T, r, sigma;
    };
    std::vector<Input> inputs(NUM_TESTS);
    for (auto &in : inputs)
    {
        in = {dist(gen), dist(gen), 0.5, 0.05, 0.2};
    }

    // 2. Measure Determinism
    // If outputs change between runs on the same input, it is non-deterministic
    std::vector<OptionState> run1(NUM_TESTS), run2(NUM_TESTS);

    for (size_t i = 0; i < NUM_TESTS; ++i)
        run1[i] = compute_option_state(CallTag{}, inputs[i].S, inputs[i].K, inputs[i].T, inputs[i].r, inputs[i].sigma);

    for (size_t i = 0; i < NUM_TESTS; ++i)
        run2[i] = compute_option_state(CallTag{}, inputs[i].S, inputs[i].K, inputs[i].T, inputs[i].r, inputs[i].sigma);

    bool is_deterministic = true;
    for (size_t i = 0; i < NUM_TESTS; ++i)
    {
        if (run1[i].price != run2[i].price)
            is_deterministic = false;
    }
    std::cout << "Determinism Test: " << (is_deterministic ? "PASSED" : "FAILED") << std::endl;

    // 3. Performance & Precision Measurement
    auto start = std::chrono::high_resolution_clock::now();

    double max_error = 0.0;
    for (size_t i = 0; i < NUM_TESTS; ++i)
    {
        auto res = compute_option_state(CallTag{}, inputs[i].S, inputs[i].K, inputs[i].T, inputs[i].r, inputs[i].sigma);

        // Simple Precision Check (Comparing against std::erf)
        // Note: This is an approximation of error since your BS formula uses fast_erf
        double error = std::abs(res.delta - reference_cdf((std::log(inputs[i].S / inputs[i].K) + (0.05 + 0.5 * 0.04) * 0.5) / (0.2 * std::sqrt(0.5))));
        if (error > max_error)
            max_error = error;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Throughput: " << (NUM_TESTS / elapsed.count()) / 1'000'000.0 << " million opts/sec" << std::endl;
    std::cout << "Max Precision Error vs std::erf: " << max_error << std::endl;

    return 0;
}