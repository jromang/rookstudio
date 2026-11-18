#pragma once

// Engine lines held to the tablebases (the prototype's crates/analysis/src/exact.rs): where the outcome of a position
// is known exactly, the engine's scores give way to it, and its first choice to a move that keeps it. An engine that
// reads the tables itself needs none of this; one that does not, or tables the engine cannot read (what an online
// tablebase taught the program), do.

#include "eval.hpp"
#include "position.hpp"
#include "tablebase.hpp"

namespace fcg::commentary {

// Holds the analysis of `position` to the tables, when they know it: every line gets the exact outcome of its first
// move, the lines are put in the order of their outcomes, and a move that keeps the position's outcome leads them, with
// a second one after it when there is one, so that an only move is one in truth. Whether anything changed.
bool settle(const Position& position, PositionAnalysis& analysis, const Tablebases& tables);

}  // namespace fcg::commentary
