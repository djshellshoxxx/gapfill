#include "TraceQueue.h"
#include <iostream>
#include <thread>

using namespace gapfill::transportprobe;
int main()
{
    Queue<4> bounded;
    Row row;
    for (std::uint64_t sequence = 0; sequence < 3; ++sequence)
    { row.sequence = sequence; if (! bounded.push (row)) return 1; }
    if (bounded.push (row) || bounded.droppedRows() != 1) return 2;
    for (std::uint64_t sequence = 0; sequence < 3; ++sequence)
        if (! bounded.pop (row) || row.sequence != sequence) return 3;
    if (bounded.pop (row)) return 4;

    Gate gate;
    gate.open();
    if (! gate.enter()) return 5;
    gate.close();
    if (gate.closed() || gate.enter()) return 6;
    gate.leave();
    if (! gate.closed() || gate.enter()) return 7;

    Queue<> concurrent;
    std::atomic<bool> done { false };
    constexpr std::uint64_t attempts = 200000;
    std::thread producer ([&]
    {
        for (std::uint64_t sequence = 0; sequence < attempts; ++sequence)
        { Row value; value.sequence = sequence; (void) concurrent.push (value); }
        done.store (true, std::memory_order_release);
    });
    std::uint64_t received = 0, previous = 0;
    bool ordered = true;
    while (! done.load (std::memory_order_acquire))
        if (concurrent.pop (row))
        { ordered = ordered && (received == 0 || row.sequence > previous); previous = row.sequence; ++received; }
    producer.join();
    while (concurrent.pop (row))
    { ordered = ordered && (received == 0 || row.sequence > previous); previous = row.sequence; ++received; }
    if (! ordered || received + concurrent.droppedRows() != attempts) return 8;
    std::cout << "bounded queue, close handshake and 200000 attempted rows accounted for\n";
}
