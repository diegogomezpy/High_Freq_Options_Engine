#pragma once

#include <cmath>
#include <numbers>
#include <algorithm>

namespace HFT::math
{
    // ========================================================================
    // Compile-Time Constants
    // ========================================================================

    constexpr double INV_SQRT_2PI = std::numbers::inv_sqrtpi_v<double> / std::numbers::sqrt2_v<double>;
    constexpr double INV_SQRT2 = 1.0 / std::numbers::sqrt2_v<double>;

    // ========================================================================
    // Compile-Time Poly-Type Tags
    // ========================================================================

    struct CallTag
    {
    };
    struct PutTag
    {
    };

    // ========================================================================
    // Packed Performance Structures
    // ========================================================================

    struct alignas(32) OptionState
    {
        double price;
        double delta;
        double vega;
        double gamma;
        double theta;
        double rho;
    };

    // ========================================================================
    // Core Math & Statistical Primitives
    // ========================================================================

    [[nodiscard]] inline double fast_erf(double x) noexcept
    {
        const double sign = (x < 0) ? -1.0 : 1.0;
        x = std::abs(x);
        const double t = 1.0 / (1.0 + 0.3275911 * x);
        const double y = 1.0 - (((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t) * std::exp(-x * x);
        return sign * y;
    }

    [[nodiscard]] inline double normal_pdf(double x) noexcept
    {
        return INV_SQRT_2PI * std::exp(-0.5 * x * x);
    }

    [[nodiscard]] inline double normal_cdf(double x) noexcept
    {
        return 0.5 * (1.0 + fast_erf(x * INV_SQRT2));
    }

    // ========================================================================
    // Option State Computation
    // ========================================================================

    namespace detail
    {
        struct BSCore
        {
            double d_1;
            double d_2;
            double sqrt_t;
            double discount_factor;
        };

        [[nodiscard]] inline BSCore bs_core(double S, double K, double T, double r, double sigma) noexcept
        {
            const double sqrt_t = std::sqrt(T);
            const double vol_sqrt_t = sigma * sqrt_t;
            const double d_1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / vol_sqrt_t;
            const double d_2 = d_1 - vol_sqrt_t;
            return BSCore{d_1, d_2, sqrt_t, std::exp(-r * T)};
        }
    }

    [[nodiscard]] inline OptionState compute_option_state(CallTag, double S, double K, double T, double r, double sigma) noexcept
    {
        if (sigma <= 0 || T <= 0 || S <= 0 || K <= 0) [[unlikely]]
            return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

        const auto core = detail::bs_core(S, K, T, r, sigma);
        const double n_d_1 = normal_cdf(core.d_1);
        const double n_d_2 = normal_cdf(core.d_2);
        const double pdf_d_1 = normal_pdf(core.d_1);

        return OptionState{
            .price = (S * n_d_1) - (K * core.discount_factor * n_d_2),
            .delta = n_d_1,
            .vega = S * pdf_d_1 * core.sqrt_t,
            .gamma = pdf_d_1 / (S * sigma * core.sqrt_t),
            .theta = (-((S * pdf_d_1 * sigma) / (2.0 * core.sqrt_t)) - (r * K * core.discount_factor * n_d_2)) / 365.0,
            .rho = (K * T * core.discount_factor * n_d_2) / 100.0};
    }

    [[nodiscard]] inline OptionState compute_option_state(PutTag, double S, double K, double T, double r, double sigma) noexcept
    {
        if (sigma <= 0 || T <= 0 || S <= 0 || K <= 0) [[unlikely]]
            return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

        const auto core = detail::bs_core(S, K, T, r, sigma);
        const double n_neg_d_1 = normal_cdf(-core.d_1);
        const double n_neg_d_2 = normal_cdf(-core.d_2);
        const double pdf_d_1 = normal_pdf(core.d_1);

        return OptionState{
            .price = (K * core.discount_factor * n_neg_d_2) - (S * n_neg_d_1),
            .delta = n_neg_d_1 - 1.0,
            .vega = S * pdf_d_1 * core.sqrt_t,
            .gamma = pdf_d_1 / (S * sigma * core.sqrt_t),
            .theta = (-((S * pdf_d_1 * sigma) / (2.0 * core.sqrt_t)) + (r * K * core.discount_factor * n_neg_d_2)) / 365.0,
            .rho = -(K * T * core.discount_factor * n_neg_d_2) / 100.0};
    }

} // namespace HFT::math