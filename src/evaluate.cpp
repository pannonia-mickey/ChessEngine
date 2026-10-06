#include "evaluate.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <type_traits>

#include "bitboard.hpp"
#include "eval_values.hpp"
#include "magic.hpp"

namespace chess {
namespace {

using eval::kParamValues;

constexpr std::array<int, kPieceTypeCount> kPhaseWeight = {0, 1, 1, 2, 4, 0};

constexpr std::array<int, kPieceTypeCount> kMobility = {
    0, eval::kKnightMobility, eval::kBishopMobility, eval::kRookMobility, eval::kQueenMobility, 0};

constexpr int psqt_index(Color color, PieceType type, Square square) {
    return eval::kPsqt + (type * kSquareCount) + (color == White ? square ^ 56 : square);
}

// Material and position of every piece on every square, from White's point of view (Black's
// entries are negative), folded together at compile time.
consteval std::array<std::array<PhaseScore, kSquareCount>, kPieceCount> make_psqt() {
    std::array<std::array<PhaseScore, kSquareCount>, kPieceCount> psqt{};
    for (const Color color : {White, Black}) {
        const int sign = color == White ? 1 : -1;
        for (int type = 0; type < kPieceTypeCount; ++type) {
            const PhaseScore& material = kParamValues.at(static_cast<std::size_t>(type));
            for (int square = 0; square < kSquareCount; ++square) {
                const PhaseScore& position = kParamValues.at(
                    static_cast<std::size_t>(psqt_index(color, PieceType(type), Square(square))));
                psqt.at(make_piece(color, PieceType(type))).at(static_cast<std::size_t>(square)) = {
                    .mg = sign * (material.mg + position.mg),
                    .eg = sign * (material.eg + position.eg)};
            }
        }
    }
    return psqt;
}

constexpr auto kPsqt = make_psqt();

constexpr Bitboard adjacent_files(int file) {
    return (file > 0 ? file_bb(file - 1) : 0) | (file < 7 ? file_bb(file + 1) : 0);
}

// The ranks strictly in front of a square, as seen from `color`.
constexpr Bitboard forward_ranks(Color color, Square square) {
    const int rank = rank_of(square);
    if (color == White) {
        return rank == 7 ? 0 : ~Bitboard{0} << (8 * (rank + 1));
    }
    return (Bitboard{1} << (8 * rank)) - 1;
}

constexpr Bitboard pawn_attacks_bb(Color color, Bitboard pawns) {
    const Bitboard sideways = shift_east(pawns) | shift_west(pawns);
    return color == White ? shift_north(sideways) : shift_south(sideways);
}

constexpr int distance(Square a, Square b) {
    return std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b)));
}

// The attacks of a knight, bishop, rook or queen. piece_attacks() does the same, but this one
// can be inlined into the evaluation.
inline Bitboard attacks_of(PieceType type, Square square, Bitboard occupied) {
    switch (type) {
        case Knight:
            return knight_attacks(square);
        case Bishop:
            return bishop_attacks(square, occupied);
        case Rook:
            return rook_attacks(square, occupied);
        default:
            return queen_attacks(square, occupied);
    }
}

struct NoTrace {};

// Sums the terms from White's point of view. With Trace set it records how often each term
// occurs instead of adding up its value.
template <bool Trace>
class Evaluator {
public:
    explicit Evaluator(const Position& pos) : pos_(pos) {}

    Score run() {
        for (const Color color : {White, Black}) {
            pawn_attacks_[color] = pawn_attacks_bb(color, pos_.pieces(color, Pawn));
        }
        material_and_psqt();
        for (const Color color : {White, Black}) {
            pawns(color);
            pieces(color);
            king(color);
        }
        const int phase = game_phase(pos_);
        if constexpr (Trace) {
            trace_.phase = phase;
        }
        return ((mg_ * phase) + (eg_ * (kMaxPhase - phase))) / kMaxPhase;
    }

    [[nodiscard]] const EvalTrace& trace() const
        requires Trace
    {
        return trace_;
    }

private:
    void add(Color color, int index, int count = 1) {
        const int signed_count = color == White ? count : -count;
        const auto i = static_cast<std::size_t>(index);
        if constexpr (Trace) {
            trace_.coefficients[i] += signed_count;
        } else {
            mg_ += kParamValues[i].mg * signed_count;
            eg_ += kParamValues[i].eg * signed_count;
        }
    }

    void material_and_psqt() {
        Bitboard occupied = pos_.pieces();
        while (occupied != 0) {
            const Square square = pop_lsb(occupied);
            const Piece piece = pos_.piece_on(square);
            if constexpr (Trace) {
                add(color_of(piece), eval::kMaterial + type_of(piece));
                add(color_of(piece), psqt_index(color_of(piece), type_of(piece), square));
            } else {
                mg_ += kPsqt[piece][square].mg;
                eg_ += kPsqt[piece][square].eg;
            }
        }
    }

    void pawns(Color us) {
        const Color them = ~us;
        const Bitboard ours = pos_.pieces(us, Pawn);
        const Bitboard theirs = pos_.pieces(them, Pawn);
        Bitboard remaining = ours;
        while (remaining != 0) {
            const Square square = pop_lsb(remaining);
            const int file = file_of(square);
            const int rank = relative_rank(us, square);
            const Bitboard forward = forward_ranks(us, square);
            const Bitboard adjacent = adjacent_files(file);
            // Only the front pawn of a doubled pair can be passed.
            const bool doubled = (forward & file_bb(file) & ours) != 0;
            const bool passed = !doubled && (forward & (adjacent | file_bb(file)) & theirs) == 0;

            if (doubled) {
                add(us, eval::kDoubledPawn);
            }
            if ((adjacent & ours) == 0) {
                add(us, eval::kIsolatedPawn);
            } else if (!passed && rank < 7 && (adjacent & ~forward & ours) == 0 &&
                       has(pawn_attacks_[them], offset(square, pawn_push(us)))) {
                // No pawn of ours can defend it, and it cannot advance safely.
                add(us, eval::kBackwardPawn);
            }
            if (((shift_east(square_bb(square)) | shift_west(square_bb(square))) & ours) != 0) {
                add(us, eval::kPhalanxPawn + rank);
            }
            if (has(pawn_attacks_[us], square)) {
                add(us, eval::kSupportedPawn + rank);
            }
            if (passed) {
                passed_pawn(us, square, rank);
            }
        }
    }

    void passed_pawn(Color us, Square square, int rank) {
        add(us, eval::kPassedPawn + rank);
        if (rank == 7) {
            return;
        }
        const Square stop = offset(square, pawn_push(us));
        if (pos_.piece_on(stop) != NoPiece) {
            add(us, eval::kPassedBlocked + rank);
        }
        add(us, eval::kPassedOwnKingDistance + distance(pos_.king_square(us), stop));
        add(us, eval::kPassedEnemyKingDistance + distance(pos_.king_square(~us), stop));
    }

    void pieces(Color us) {
        const Color them = ~us;
        const Bitboard occupied = pos_.pieces();
        const Bitboard mobility_area = ~pos_.pieces(us) & ~pawn_attacks_[them];
        const Square enemy_king = pos_.king_square(them);
        const Bitboard king_zone = king_attacks(enemy_king) | square_bb(enemy_king);
        int attackers = 0;
        for (const PieceType type : {Knight, Bishop, Rook, Queen}) {
            Bitboard remaining = pos_.pieces(us, type);
            while (remaining != 0) {
                const Square square = pop_lsb(remaining);
                const Bitboard attacks = attacks_of(type, square, occupied);
                add(us, kMobility[type] + popcount(attacks & mobility_area));
                if (const Bitboard zone_attacks = attacks & king_zone; zone_attacks != 0) {
                    ++attackers;
                    add(us, eval::kKingZoneAttack + type - Knight, popcount(zone_attacks));
                }
                if (type == Rook) {
                    const Bitboard file = file_bb(file_of(square));
                    if ((file & pos_.pieces(Pawn)) == 0) {
                        add(us, eval::kRookOpenFile);
                    } else if ((file & pos_.pieces(us, Pawn)) == 0) {
                        add(us, eval::kRookSemiOpenFile);
                    }
                }
            }
            if (const int threatened = popcount(pawn_attacks_[us] & pos_.pieces(them, type));
                threatened != 0) {
                add(us, eval::kThreatByPawn + type - Knight, threatened);
            }
        }
        add(us, eval::kKingAttackers + std::min(attackers, 3));
        if (more_than_one(pos_.pieces(us, Bishop))) {
            add(us, eval::kBishopPair);
        }
    }

    void king(Color us) {
        const Square king = pos_.king_square(us);
        const Bitboard ours = pos_.pieces(us, Pawn);
        const int file = file_of(king);
        const Bitboard shield_files = file_bb(file) | adjacent_files(file);
        for (int step = 1; step <= 2 && relative_rank(us, king) + step < 8; ++step) {
            const int rank = rank_of(king) + (us == White ? step : -step);
            if (const int count = popcount(ours & shield_files & rank_bb(rank)); count != 0) {
                add(us, eval::kPawnShield + step - 1, count);
            }
        }
        if ((file_bb(file) & ours) == 0) {
            add(us, (file_bb(file) & pos_.pieces(Pawn)) == 0 ? eval::kKingOpenFile
                                                             : eval::kKingSemiOpenFile);
        }
    }

    const Position& pos_;
    std::array<Bitboard, kColorCount> pawn_attacks_{};
    Score mg_ = 0;
    Score eg_ = 0;
    [[no_unique_address]] std::conditional_t<Trace, EvalTrace, NoTrace> trace_{};
};

}  // namespace

int game_phase(const Position& pos) {
    int phase = 0;
    for (const PieceType type : {Knight, Bishop, Rook, Queen}) {
        phase += popcount(pos.pieces(type)) * kPhaseWeight[type];
    }
    // Early promotions can push the count past the opening value.
    return std::min(phase, kMaxPhase);
}

Score piece_value(PieceType type) {
    return type == NoPieceType ? 0 : kParamValues[type].mg;
}

Score evaluate(const Position& pos) {
    const Score white_score = Evaluator<false>(pos).run();
    return pos.side_to_move() == White ? white_score : -white_score;
}

EvalTrace trace_evaluation(const Position& pos) {
    Evaluator<true> evaluator(pos);
    static_cast<void>(evaluator.run());
    return evaluator.trace();
}

}  // namespace chess
