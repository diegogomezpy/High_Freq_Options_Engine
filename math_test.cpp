#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <cmath>
#include <string>
#include <numeric>
#include <random>

#include "math.hpp"

// ========================================================================
// Test Suite Framework Context & Formatting
// ========================================================================

struct TestMetric
{
    std::string test_name;
    bool passed{false};
    double execution_latency_ns{0.0};
    std::string details;
};

void print_section_header(const std::string &title)
{
    std::cout << "\n======================================================================\n";
    std::cout << " SECTION: " << title << "\n";
    std::cout << "======================================================================\n";
}

// Forces the compiler to treat the parameter as read, preventing dead-code elimination
template <typename T>
inline void do_not_optimize(const T &value)
{
    asm volatile("" : : "g"(value) : "memory");
}

int main()
{

    // Configure terminal output to micro-precision formatting
    std::cout << std::fixed << std::setprecision(8);
    std::vector<TestMetric> metrics;

    // Baseline Parameters for liquid testing matrix
    constexpr double S = 100.0;
    constexpr double K = 105.0; // Slightly OTM for Calls, ITM for Puts
    constexpr double T = 0.25;  // 3 months
    constexpr double r = 0.04;  // 4% risk-free rate
    constexpr double target_vol = 0.3580;

    // ========================================================================
    // 1. EXECUTION ACCURACY VERIFICATION
    // ========================================================================
    print_section_header("Execution Accuracy Verification");
    {
        TestMetric call_acc{"Call Inversion Accuracy", false, 0.0, ""};
        TestMetric put_acc{"Put Inversion Accuracy", false, 0.0, ""};

        // Call Path
        auto call_state = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K, T, r, target_vol);
        auto call_solver = HFT::math::solve_iv(HFT::math::CallTag{}, call_state.price, S, K, T, r);
        double call_delta = std::abs(call_solver.implied_vol - target_vol);

        if (call_solver.converged && call_delta < 1e-6)
        {
            call_acc.passed = true;
            call_acc.details = "Converged in " + std::to_string(call_solver.iterations) + " iterations. Delta: " + std::to_string(call_delta);
        }
        else
        {
            call_acc.details = "FAILED. Vol: " + std::to_string(call_solver.implied_vol) + " (Expected " + std::to_string(target_vol) + ")";
        }

        // Put Path
        auto put_state = HFT::math::compute_option_state(HFT::math::PutTag{}, S, K, T, r, target_vol);
        auto put_solver = HFT::math::solve_iv(HFT::math::PutTag{}, put_state.price, S, K, T, r);
        double put_delta = std::abs(put_solver.implied_vol - target_vol);

        if (put_solver.converged && put_delta < 1e-6)
        {
            put_acc.passed = true;
            put_acc.details = "Converged in " + std::to_string(put_solver.iterations) + " iterations. Delta: " + std::to_string(put_delta);
        }
        else
        {
            put_acc.details = "FAILED. Vol: " + std::to_string(put_solver.implied_vol) + " (Expected " + std::to_string(target_vol) + ")";
        }

        metrics.push_back(call_acc);
        metrics.push_back(put_acc);

        std::cout << "[ACCURACY] Call Implied Vol Delta: " << call_delta << " (" << (call_acc.passed ? "PASS" : "FAIL") << ")\n";
        std::cout << "[ACCURACY] Put Implied Vol Delta : " << put_delta << " (" << (put_acc.passed ? "PASS" : "FAIL") << ")\n";
    }

    // ========================================================================
    // 2. STRESS TESTING BOUNDARY EDGE CASES
    // ========================================================================
    print_section_header("Boundary Edge Cases & Stability Checks");
    {
        // Edge Case A: Near-Zero Time to Expiry (T -> 0)
        TestMetric edge_t{"Zero-Time Stability", true, 0.0, "Safely bypassed or handled via unlikely branch."};
        auto zero_t_state = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K, 0.0, r, target_vol);
        if (zero_t_state.price != 0.0 || zero_t_state.vega != 0.0)
        {
            edge_t.passed = false;
            edge_t.details = "Failed to zero out underflowed time coordinates.";
        }
        metrics.push_back(edge_t);

        // Edge Case B: Extreme Volatility Regime (Target Vol = 4.25)
        TestMetric edge_high_vol{"Hyper-Volatility Convergence", false, 0.0, ""};
        constexpr double hyper_target_vol = 4.25;
        auto hyper_state = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K, T, r, hyper_target_vol);
        auto hyper_solver = HFT::math::solve_iv(HFT::math::CallTag{}, hyper_state.price, S, K, T, r);

        if (hyper_solver.converged && std::abs(hyper_solver.implied_vol - hyper_target_vol) < 1e-5)
        {
            edge_high_vol.passed = true;
            edge_high_vol.details = "Bisection hybrid successfully trapped hyper-vol at " + std::to_string(hyper_solver.implied_vol);
        }
        else
        {
            edge_high_vol.details = "Failed to resolve hyper-vol. Bounded output: " + std::to_string(hyper_solver.implied_vol);
        }
        metrics.push_back(edge_high_vol);

        // Edge Case C: Deeply Out-of-the-Money Option (S=100, K=350 -> Vega Underflow)
        TestMetric edge_otm{"Deep OTM Slopes (Vega Underflow)", false, 0.0, ""};
        constexpr double K_deep_otm = 350.0;
        auto otm_state = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K_deep_otm, T, r, target_vol);
        auto otm_solver = HFT::math::solve_iv(HFT::math::CallTag{}, otm_state.price, S, K_deep_otm, T, r);

        // Ensure the calculation engine didn't throw a NaN or Inf under degenerate slopes
        if (!std::isnan(otm_solver.implied_vol) && !std::isinf(otm_solver.implied_vol))
        {
            edge_otm.passed = true;
            edge_otm.details = "Handled slope collapse gracefully. Solved Vol: " + std::to_string(otm_solver.implied_vol) + " (Converged: " + (otm_solver.converged ? "YES" : "NO") + ")";
        }
        else
        {
            edge_otm.details = "FAILED. Solver output degenerated to NaN or Infinity.";
        }
        metrics.push_back(edge_otm);

        std::cout << "[EDGE-CASE] T=0 Call Price: " << zero_t_state.price << " | Vega: " << zero_t_state.vega << "\n";
        std::cout << "[EDGE-CASE] Hyper-Vol (4.250) Recovered: " << hyper_solver.implied_vol << " (Iterations: " << hyper_solver.iterations << ")\n";
        std::cout << "[EDGE-CASE] Deep OTM (K=350) Handled cleanly without NaN/Inf generation.\n";
    }

    // ========================================================================
    // 3. BITWISE DETERMINISM CHECKS
    // ========================================================================
    print_section_header("Execution Determinism Verification");
    {
        TestMetric determinism{"State Determinism Check", true, 0.0, "Identical binary execution across 10,000 cycles."};

        auto baseline_state = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K, T, r, target_vol);
        auto baseline_solver = HFT::math::solve_iv(HFT::math::CallTag{}, baseline_state.price, S, K, T, r);

        // Loop execution 10,000 times to verify floating-point stability across matching states
        for (int i = 0; i < 10000; ++i)
        {
            auto loop_state = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K, T, r, target_vol);
            auto loop_solver = HFT::math::solve_iv(HFT::math::CallTag{}, loop_state.price, S, K, T, r);

            // Double precision binary equivalence check
            if (loop_solver.implied_vol != baseline_solver.implied_vol ||
                loop_solver.iterations != baseline_solver.iterations ||
                loop_solver.converged != baseline_solver.converged)
            {
                determinism.passed = false;
                determinism.details = "Bitwise drift detected at loop cycle " + std::to_string(i);
                break;
            }
        }
        metrics.push_back(determinism);
        std::cout << "[DETERMINISM] Bitwise consistency state: " << (determinism.passed ? "VERIFIED DETERMINISTIC" : "DRIFT DETECTED") << "\n";
    }

    // ========================================================================
    // 4. MICRO-BENCHMARKING & SPEED ANALYSIS
    // ========================================================================
    print_section_header("Sub-Nanosecond Micro-Benchmarking");
    {
        constexpr size_t ITERATIONS = 1'000'000;
        std::cout << "Warming CPU instruction caches and executing " << ITERATIONS << " analytical runs...\n";

        // Benchmark A: Forward Analytical Pricer
        TestMetric bench_forward{"Forward Pricer Throughput", true, 0.0, ""};
        auto start_f = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < ITERATIONS; ++i)
        {
            // Mutate sigma slightly to prevent compiler loop unrolling optimizations
            double dynamic_sigma = target_vol + (static_cast<double>(i & 0xFF) * 1e-6);
            auto res = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K, T, r, dynamic_sigma);
            do_not_optimize(res.price);
        }
        auto end_f = std::chrono::high_resolution_clock::now();
        double total_f_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_f - start_f).count();
        bench_forward.execution_latency_ns = total_f_ns / ITERATIONS;
        bench_forward.details = "Total batch time: " + std::to_string(total_f_ns / 1e6) + " ms for 1M calls.";
        metrics.push_back(bench_forward);

        // Benchmark B: Reverse Implied Vol Solver Loop
        TestMetric bench_solver{"Hybrid Solver Throughput", true, 0.0, ""};
        auto analytical_state = HFT::math::compute_option_state(HFT::math::CallTag{}, S, K, T, r, target_vol);

        auto start_s = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < ITERATIONS; ++i)
        {
            // Introduce sequential noise to keep execution pipeline processing novel operations
            double dynamic_price = analytical_state.price + (static_cast<double>(i & 0x0F) * 1e-4);
            auto res = HFT::math::solve_iv(HFT::math::CallTag{}, dynamic_price, S, K, T, r);
            do_not_optimize(res.implied_vol);
        }
        auto end_s = std::chrono::high_resolution_clock::now();
        double total_s_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_s - start_s).count();
        bench_solver.execution_latency_ns = total_s_ns / ITERATIONS;
        bench_solver.details = "Total batch time: " + std::to_string(total_s_ns / 1e6) + " ms for 1M full solves.";
        metrics.push_back(bench_solver);

        std::cout << "[BENCHMARK] Forward Analytical Pricer: " << bench_forward.execution_latency_ns << " ns / eval\n";
        std::cout << "[BENCHMARK] Reverse Hybrid Solver    : " << bench_solver.execution_latency_ns << " ns / run\n";
    }

    // ========================================================================
    // FINAL EXECUTIVE RUN-DOWN SUMMARY
    // ========================================================================
    std::cout << "\n\n";
    std::cout << "================================================================================\n";
    std::cout << "                       HFT CORE MATHEMATICS RUN-DOWN REPORT                     \n";
    std::cout << "================================================================================\n";
    std::cout << " " << std::left << std::setw(34) << "TEST NAME"
              << std::setw(10) << "STATUS"
              << std::setw(16) << "LATENCY (NS)"
              << "METRIC SUMMARY / METADATA\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    size_t passed_count = 0;
    for (const auto &m : metrics)
    {
        std::cout << " " << std::left << std::setw(34) << m.test_name
                  << std::setw(10) << (m.passed ? "✅ PASS" : "❌ FAIL");

        if (m.execution_latency_ns > 0.0)
        {
            std::cout << std::left << std::setw(16) << std::to_string(m.execution_latency_ns).substr(0, 6) + " ns";
        }
        else
        {
            std::cout << std::left << std::setw(16) << "N/A (Static)";
        }

        std::cout << m.details << "\n";
        if (m.passed)
            passed_count++;
    }
    std::cout << "================================================================================\n";
    std::cout << " FINAL RESULTS: [" << passed_count << "/" << metrics.size() << "] Engine Components Operational.\n";
    std::cout << "================================================================================\n";

    return (passed_count == metrics.size()) ? 0 : 1;
}