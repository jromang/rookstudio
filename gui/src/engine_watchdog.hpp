#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace fcg {

// Ends the wait of a game whose engine ended on its own when the end of the engine's output does not come (#145).
//
// fastchess makes an engine's pipes close-on-exec only after it made them (pipe(), then fcntl()): an engine another
// game starts between the two keeps their ends open, and the game's read of the engine, once that one ended, never
// sees the end of its output. Under a time control the engine loses on time; in nodes or depth nothing ends the wait,
// and the game and its slot stay stuck. stopEngineProcess() ends that wait for an engine the operator stops (#142);
// this ends it for an engine that ends on its own (a crash).
//
// At each look it goes through the engines fastchess lists (fastchess::process_list, under its lock). An engine whose
// process has ended (a child of ours not reaped yet), found so at the look before as well, has its game's read woken
// through the descriptor fastchess keeps for each engine, as fastchess wakes them all on a Ctrl+C
// (writeToOpenPipes()): the read returns, fastchess sees the engine disconnected, and the tournament's crash handling
// applies ("Restart an engine that crashes", or the tournament's stop without it).
// - Only an engine still listed, and once: fastchess takes an engine off the list before it reaps it
//   (Process::terminate()). One it reaped while it is still listed (alive(), from the game's thread) is not a child
//   of ours any more: its game saw it end, and its pid may be another process's.
// - A look later: a read that sees the end of the output by itself (no other process keeps it open) first reads what
//   the engine wrote before it ended (its last words on its error output), which a wake would cut short.
// - Never reaped here (waitid's WNOWAIT): fastchess reaps its engines, and its own wait would fail on one reaped.
// - A wake that comes after the game saw the engine end does no harm. Writes count as one wake (an eventfd) or as
//   many (the pipe fastchess uses without eventfd, on macOS), but only for reads of that process: fastchess asks
//   whether an engine runs before it talks to it (alive()), finds it ended and starts it anew, with a descriptor of
//   its own.
// POSIX only: on Windows it does nothing (a read there ends when its engine does).
class EngineWatchdog {
   public:
    // How often the engines of a tournament are looked at (playTournament()).
    static constexpr std::chrono::milliseconds PERIOD{250};

    // Looks every `period` on a thread of its own until destroyed; with 0, only when look() is called.
    explicit EngineWatchdog(std::chrono::milliseconds period);
    ~EngineWatchdog();
    EngineWatchdog(const EngineWatchdog&)            = delete;
    EngineWatchdog& operator=(const EngineWatchdog&) = delete;

    // Looks once: wakes the listed engines found ended at this look and at the one before. Returns how many it woke.
    int look();

   private:
    // An engine found ended at the last look, as fastchess lists it: its process, the descriptor that wakes its game.
    struct Ended {
        int pid    = 0;
        int fd     = -1;
        bool woken = false;
    };
    std::mutex look_mutex_;
    std::vector<Ended> ended_;

    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace fcg
