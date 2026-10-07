#pragma once

#include <array>
#include <string_view>

namespace chess {

// Scores are in centipawns.
using Score = int;

// A middlegame and an endgame value, blended by the game phase.
struct PhaseScore {
    Score mg = 0;
    Score eg = 0;
};

namespace eval {

// The evaluation is a sum of terms, each a parameter (a PhaseScore) times how often its feature
// occurs for White minus how often for Black. All parameters live in one flat array, so the tuner
// (tools/tuner) can treat the evaluation as a linear function of them. These offsets give each
// group of parameters its place in the array.

// Material, indexed by piece type.
inline constexpr int kMaterial = 0;
// Piece-square tables, indexed by piece type * 64 + square as seen from White, with a8 first
// (so the tables read like a board): White looks a square up with `square ^ 56`, Black with the
// square itself.
inline constexpr int kPsqt = kMaterial + 6;
// Mobility of each piece type, indexed by the number of safe squares it attacks.
inline constexpr int kKnightMobility = kPsqt + (6 * 64);
inline constexpr int kBishopMobility = kKnightMobility + 9;
inline constexpr int kRookMobility = kBishopMobility + 14;
inline constexpr int kQueenMobility = kRookMobility + 15;
// Pawn structure, the ranked terms indexed by the pawn's rank as seen from its own side.
inline constexpr int kPassedPawn = kQueenMobility + 28;
inline constexpr int kPassedBlocked = kPassedPawn + 8;
// Distance of each king to the square in front of a passed pawn, indexed by distance.
inline constexpr int kPassedOwnKingDistance = kPassedBlocked + 8;
inline constexpr int kPassedEnemyKingDistance = kPassedOwnKingDistance + 8;
inline constexpr int kPhalanxPawn = kPassedEnemyKingDistance + 8;
inline constexpr int kSupportedPawn = kPhalanxPawn + 8;
inline constexpr int kDoubledPawn = kSupportedPawn + 8;
inline constexpr int kIsolatedPawn = kDoubledPawn + 1;
inline constexpr int kBackwardPawn = kIsolatedPawn + 1;
// Pieces.
inline constexpr int kBishopPair = kBackwardPawn + 1;
inline constexpr int kRookOpenFile = kBishopPair + 1;
inline constexpr int kRookSemiOpenFile = kRookOpenFile + 1;
// Attacks of our pawns on enemy pieces, indexed by the attacked piece type (knight to queen).
inline constexpr int kThreatByPawn = kRookSemiOpenFile + 1;
// King safety. The attack terms score the attacking side: squares of the enemy king zone
// attacked, per attacking piece type (knight to queen), and the number of pieces attacking it
// (three or more share the last entry).
inline constexpr int kKingZoneAttack = kThreatByPawn + 4;
inline constexpr int kKingAttackers = kKingZoneAttack + 4;
// Own pawns one and two ranks in front of the king, on its file and the adjacent ones.
inline constexpr int kPawnShield = kKingAttackers + 4;
inline constexpr int kKingOpenFile = kPawnShield + 2;
inline constexpr int kKingSemiOpenFile = kKingOpenFile + 1;
inline constexpr int kParamCount = kKingSemiOpenFile + 1;

// A named group of parameters, for printing them.
struct ParamGroup {
    std::string_view name;
    int offset = 0;
    int size = 0;
};

inline constexpr std::array<ParamGroup, 29> kParamGroups = {{
    {.name = "Material", .offset = kMaterial, .size = 6},
    {.name = "Pawn PSQT", .offset = kPsqt, .size = 64},
    {.name = "Knight PSQT", .offset = kPsqt + 64, .size = 64},
    {.name = "Bishop PSQT", .offset = kPsqt + (2 * 64), .size = 64},
    {.name = "Rook PSQT", .offset = kPsqt + (3 * 64), .size = 64},
    {.name = "Queen PSQT", .offset = kPsqt + (4 * 64), .size = 64},
    {.name = "King PSQT", .offset = kPsqt + (5 * 64), .size = 64},
    {.name = "Knight mobility", .offset = kKnightMobility, .size = 9},
    {.name = "Bishop mobility", .offset = kBishopMobility, .size = 14},
    {.name = "Rook mobility", .offset = kRookMobility, .size = 15},
    {.name = "Queen mobility", .offset = kQueenMobility, .size = 28},
    {.name = "Passed pawn", .offset = kPassedPawn, .size = 8},
    {.name = "Passed pawn blocked", .offset = kPassedBlocked, .size = 8},
    {.name = "Passed pawn, own king distance", .offset = kPassedOwnKingDistance, .size = 8},
    {.name = "Passed pawn, enemy king distance", .offset = kPassedEnemyKingDistance, .size = 8},
    {.name = "Phalanx pawn", .offset = kPhalanxPawn, .size = 8},
    {.name = "Supported pawn", .offset = kSupportedPawn, .size = 8},
    {.name = "Doubled pawn", .offset = kDoubledPawn, .size = 1},
    {.name = "Isolated pawn", .offset = kIsolatedPawn, .size = 1},
    {.name = "Backward pawn", .offset = kBackwardPawn, .size = 1},
    {.name = "Bishop pair", .offset = kBishopPair, .size = 1},
    {.name = "Rook on open file", .offset = kRookOpenFile, .size = 1},
    {.name = "Rook on semi-open file", .offset = kRookSemiOpenFile, .size = 1},
    {.name = "Threat by pawn", .offset = kThreatByPawn, .size = 4},
    {.name = "King zone attack", .offset = kKingZoneAttack, .size = 4},
    {.name = "King attackers", .offset = kKingAttackers, .size = 4},
    {.name = "Pawn shield", .offset = kPawnShield, .size = 2},
    {.name = "King on open file", .offset = kKingOpenFile, .size = 1},
    {.name = "King on semi-open file", .offset = kKingSemiOpenFile, .size = 1},
}};
static_assert(kParamGroups.back().offset + kParamGroups.back().size == kParamCount);

}  // namespace eval
}  // namespace chess
