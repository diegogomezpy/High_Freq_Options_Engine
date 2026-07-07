#include "synch.hpp" // Pulls in our unified header-only queue
#include <iostream>
#include <thread>

// Thread A: The Simulator (Producer)
void run_simulator(HFT::synchronization::ConcurrentQueue &queue)
{
    std::cout << "[Simulator] Thread launched.\n";
    // We will populate this with simulated market ticks next
}

// Thread B: The Strategy Engine (Consumer)
void run_strategy(HFT::synchronization::ConcurrentQueue &queue)
{
    std::cout << "[Strategy] Thread launched.\n";
    // We will populate this with mathematical execution logic next
}

int main()
{
    std::cout << "====================================================\n";
    std::cout << "      INITIALIZING MULTI-THREADED HFT ENGINE        \n";
    std::cout << "====================================================\n";

    // 1. Instantiate our shared communication pipeline
    HFT::synchronization::ConcurrentQueue market_queue;

    // 2. Spawn our parallel workers and link them via the queue
    std::thread simulator_thread(run_simulator, std::ref(market_queue));
    std::thread strategy_thread(run_strategy, std::ref(market_queue));

    // 3. Wait for the workers to complete before shutting down the app
    simulator_thread.join();
    strategy_thread.join();

    std::cout << "[System] Core pipelines drained. Shutting down.\n";
    return 0;
}