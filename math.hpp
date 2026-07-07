#pragma once

#include <cmath>
#include <numbers>
#include <algorithm>
#include <type_traits>

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
    // Operational Layouts
    // ========================================================================

    /**
     * @brief Minimal operational state to eliminate register spilling in hot loops.
     */
    struct alignas(16) SolverState
    {
        double price;
        double vega;
    };

    /**
     * @brief Complete options risk layout aligned explicitly to a hardware cache line.
     */
    struct alignas(64) OptionState
    {
        double price;
        double delta;
        double vega;
        double gamma;
        double theta;
        double rho;

        // Explicit alignment padding to ensure 64-byte saturation
        double padding[2];
    };

    // ========================================================================
    // Core Math & Statistical Primitives
    // ========================================================================
    [[nodiscard]] inline double normal_pdf(double x) noexcept
    {
        return INV_SQRT_2PI * std::exp(-0.5 * x * x);
    }

    [[nodiscard]] inline double normal_cdf(double x) noexcept
    {
        return 0.5 * (1.0 + std::erf(x * INV_SQRT2));
    }

    // ========================================================================
    // Invariant Acceleration Storage & Internal Primitives
    // ========================================================================
    namespace detail
    {
        struct SolverInvariants
        {
            double log_s_over_k;
            double sqrt_t;
            double discount_factor;
            double annualized_rate_t;
        };

        /**
         * @brief Hot-Loop Invariant Accelerated Overload: Handles Calls and Puts uniformly.
         * Uses compile-time if constexpr branch stripping to preserve raw HFT speed.
         */
        template <typename Tag>
        [[nodiscard]] inline SolverState compute_option_state(
            Tag,
            double S,
            double K,
            double sigma,
            const detail::SolverInvariants &inv) noexcept
        {
            const double vol_sqrt_t = sigma * inv.sqrt_t;

            const double d_1 = (inv.log_s_over_k + inv.annualized_rate_t + (0.5 * sigma * sigma * (inv.sqrt_t * inv.sqrt_t))) / vol_sqrt_t;
            const double d_2 = d_1 - vol_sqrt_t;
            const double pdf_d_1 = normal_pdf(d_1);
            const double vega_val = S * pdf_d_1 * inv.sqrt_t;

            if constexpr (std::is_same_v<Tag, CallTag>)
            {
                return SolverState{
                    .price = (S * normal_cdf(d_1)) - (K * inv.discount_factor * normal_cdf(d_2)),
                    .vega = vega_val};
            }
            else if constexpr (std::is_same_v<Tag, PutTag>)
            {
                return SolverState{
                    .price = (K * inv.discount_factor * normal_cdf(-d_2)) - (S * normal_cdf(-d_1)),
                    .vega = vega_val};
            }
        }
    } // namespace detail

    // ========================================================================
    // High-Volume Forward Pricing Overload
    // ========================================================================

    /**
     * @brief Forward analytical interface calculating complete risk matrix profiles.
     */
    template <typename Tag>
    [[nodiscard]] inline OptionState compute_option_state(
        Tag, double S, double K, double T, double r, double sigma) noexcept
    {
        if (sigma <= 0.0 || T <= 0.0 || S <= 0.0 || K <= 0.0) [[unlikely]]
        {
            return OptionState{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, {0.0, 0.0}};
        }

        const double sqrt_t = std::sqrt(T);
        const double discount_factor = std::exp(-r * T);
        const double vol_sqrt_t = sigma * sqrt_t;

        const double d_1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / vol_sqrt_t;
        const double d_2 = d_1 - vol_sqrt_t;

        const double pdf_d_1 = normal_pdf(d_1);
        const double vega_val = S * pdf_d_1 * sqrt_t;
        const double gamma_val = pdf_d_1 / (S * vol_sqrt_t);

        if constexpr (std::is_same_v<Tag, CallTag>)
        {
            const double n_d_1 = normal_cdf(d_1);
            const double n_d_2 = normal_cdf(d_2);
            return OptionState{
                .price = (S * n_d_1) - (K * discount_factor * n_d_2),
                .delta = n_d_1,
                .vega = vega_val,
                .gamma = gamma_val,
                .theta = (-((S * pdf_d_1 * sigma) / (2.0 * sqrt_t)) - (r * K * discount_factor * n_d_2)) / 365.0,
                .rho = (K * T * discount_factor * n_d_2) / 100.0,
                .padding = {0.0, 0.0}};
        }
        else
        {
            const double n_neg_d_1 = normal_cdf(-d_1);
            const double n_neg_d_2 = normal_cdf(-d_2);
            return OptionState{
                .price = (K * discount_factor * n_neg_d_2) - (S * n_neg_d_1),
                .delta = n_neg_d_1 - 1.0,
                .vega = vega_val,
                .gamma = gamma_val,
                .theta = (-((S * pdf_d_1 * sigma) / (2.0 * sqrt_t)) + (r * K * discount_factor * n_neg_d_2)) / 365.0,
                .rho = (-K * T * discount_factor * n_neg_d_2) / 100.0,
                .padding = {0.0, 0.0}};
        }
    }

    // ========================================================================
    // Solver Configuration & Metadata
    // ========================================================================

    struct SolverConfig
    {
        double max_error{1e-8};
        uint32_t max_iter{20};
        double vol_guess{0.20};
        double min_bound{1e-6};
        double max_bound{5.00};
    };

    struct SolverResult
    {
        double implied_vol;
        uint32_t iterations;
        bool converged;
    };

    // ========================================================================
    // Ultimate Optimized Hybrid Solver
    // ========================================================================

    template <typename Tag>
    [[nodiscard]] inline SolverResult solve_iv(
        Tag tag,
        double market_price,
        double S,
        double K,
        double T,
        double r,
        SolverConfig config = SolverConfig{}) noexcept
    {

        const detail::SolverInvariants invariants{
            .log_s_over_k = std::log(S / K),
            .sqrt_t = std::sqrt(T),
            .discount_factor = std::exp(-r * T),
            .annualized_rate_t = r * T};

        uint32_t num = 0;
        double vol_n0 = config.vol_guess;

        const double moneyness = std::abs((S - K) / S);
        if (moneyness <= 0.08)
        {
            double atm_seed = (market_price / S) * (2.5066282746310002 / invariants.sqrt_t);
            if (atm_seed >= config.min_bound && atm_seed <= config.max_bound && !std::isnan(atm_seed)) [[likely]]
            {
                vol_n0 = atm_seed;
            }
        }
        bool converged = false;

        double vol_low = config.min_bound;
        double vol_high = config.max_bound;

        while (num < config.max_iter)
        {
            SolverState option = compute_option_state(tag, S, K, vol_n0, invariants);

            double price_error = option.price - market_price;

            if (std::abs(price_error) <= config.max_error)
            {
                converged = true;

                break;
            }

            if (price_error > 0.0)
            {
                vol_high = vol_n0;
            }
            else
            {
                vol_low = vol_n0;
            }

            if (option.vega < 1e-8) [[unlikely]]
            {
                vol_n0 = 0.5 * (vol_high + vol_low);
            }
            else
            {
                const double vol_n1 = vol_n0 - (price_error / option.vega);

                if (vol_n1 <= vol_low || vol_n1 >= vol_high) [[unlikely]]
                {
                    vol_n0 = 0.5 * (vol_high + vol_low);
                }
                else
                {
                    vol_n0 = vol_n1;
                }
            }

            ++num;
        }

        return SolverResult{
            .implied_vol = vol_n0,
            .iterations = num,
            .converged = converged};
    }
} // namespace HFT::math