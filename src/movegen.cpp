#include "movegen.hpp"

#include <optional>
#include <string_view>

#include "bitboard.hpp"
#include "magic.hpp"

namespace chess {
namespace {

// Everything the generator needs to know about the side to move, computed once per call.
struct Context {
    const Position& pos;
    Color us;
    Square king;
    Bitboard ours;
    Bitboard theirs;
    Bitboard occupied;
    Bitboard pinned;
    // Squares a non-king move must land on: anywhere when not in check, otherwise the checker
    // or a square between it and the king.
    Bitboard target;
};

// Our pieces that are the only blocker between our king and an enemy slider.
Bitboard pinned_pieces(const Position& pos, Color us, Square king) {
    const Color them = ~us;
    const Bitboard occupied = pos.pieces();
    Bitboard snipers =
        (rook_attacks(king, 0) & (pos.pieces(them, Rook) | pos.pieces(them, Queen))) |
        (bishop_attacks(king, 0) & (pos.pieces(them, Bishop) | pos.pieces(them, Queen)));
    Bitboard pinned = 0;
    while (snipers != 0) {
        const Square sniper = pop_lsb(snipers);
        const Bitboard blockers = between_bb(king, sniper) & occupied;
        if (blockers != 0 && !more_than_one(blockers)) {
            pinned |= blockers & pos.pieces(us);
        }
    }
    return pinned;
}

void add_moves(MoveList& list, Square from, Bitboard targets) {
    while (targets != 0) {
        list.push_back(Move(from, pop_lsb(targets)));
    }
}

void add_pawn_moves(MoveList& list, Color us, Square from, Bitboard targets) {
    while (targets != 0) {
        const Square to = pop_lsb(targets);
        if (relative_rank(us, to) == 7) {
            for (const PieceType promotion : {Queen, Rook, Bishop, Knight}) {
                list.push_back(Move(from, to, MoveType::Promotion, promotion));
            }
        } else {
            list.push_back(Move(from, to));
        }
    }
}

// Squares a pinned piece may still move to: along the pin line.
Bitboard pin_mask(const Context& ctx, Square from) {
    return has(ctx.pinned, from) ? line_bb(ctx.king, from) : ~Bitboard{0};
}

void generate_pawn_moves(const Context& ctx, MoveList& list) {
    const Position& pos = ctx.pos;
    const int push = pawn_push(ctx.us);
    const Square en_passant = pos.en_passant_square();

    for (Bitboard pawns = pos.pieces(ctx.us, Pawn); pawns != 0;) {
        const Square from = pop_lsb(pawns);
        const Bitboard allowed = ctx.target & pin_mask(ctx, from);

        Bitboard targets = pawn_attacks(ctx.us, from) & ctx.theirs;
        const Square single = offset(from, push);
        if (pos.piece_on(single) == NoPiece) {
            targets |= square_bb(single);
            if (relative_rank(ctx.us, from) == 1) {
                const Square double_push = offset(single, push);
                if (pos.piece_on(double_push) == NoPiece) {
                    targets |= square_bb(double_push);
                }
            }
        }
        add_pawn_moves(list, ctx.us, from, targets & allowed);

        if (en_passant != NoSquare && has(pawn_attacks(ctx.us, from), en_passant)) {
            // Removing two pawns from one rank can expose the king along it, and the captured
            // pawn may be the checker, so test the resulting position directly.
            const Square captured = offset(en_passant, -push);
            const Bitboard occupied =
                (ctx.occupied ^ square_bb(from) ^ square_bb(captured)) | square_bb(en_passant);
            const Bitboard attackers =
                pos.attackers_to(ctx.king, occupied) & ctx.theirs & ~square_bb(captured);
            if (attackers == 0) {
                list.push_back(Move(from, en_passant, MoveType::EnPassant));
            }
        }
    }
}

void generate_piece_moves(const Context& ctx, MoveList& list) {
    for (const PieceType type : {Knight, Bishop, Rook, Queen}) {
        for (Bitboard pieces = ctx.pos.pieces(ctx.us, type); pieces != 0;) {
            const Square from = pop_lsb(pieces);
            const Bitboard targets = piece_attacks(type, from, ctx.occupied) & ~ctx.ours &
                                     ctx.target & pin_mask(ctx, from);
            add_moves(list, from, targets);
        }
    }
}

bool attacked(const Context& ctx, Square square, Bitboard occupied) {
    return (ctx.pos.attackers_to(square, occupied) & ctx.theirs) != 0;
}

void generate_king_moves(const Context& ctx, MoveList& list) {
    // The king must not hide behind itself from a slider, so it is removed from the occupancy.
    const Bitboard occupied = ctx.occupied ^ square_bb(ctx.king);
    for (Bitboard targets = king_attacks(ctx.king) & ~ctx.ours; targets != 0;) {
        const Square to = pop_lsb(targets);
        if (!attacked(ctx, to, occupied)) {
            list.push_back(Move(ctx.king, to));
        }
    }
}

void generate_castling(const Context& ctx, MoveList& list) {
    const CastlingRights rights = ctx.pos.castling_rights();
    const bool white = ctx.us == White;
    const Bitboard occupied = ctx.occupied ^ square_bb(ctx.king);

    struct Option {
        CastlingRights right;
        Square king_to;
        Bitboard must_be_empty;
        Bitboard must_be_safe;
    };
    const Option king_side{.right = white ? kWhiteKingSide : kBlackKingSide,
                           .king_to = relative_square(ctx.us, G1),
                           .must_be_empty = square_bb(relative_square(ctx.us, F1)) |
                                            square_bb(relative_square(ctx.us, G1)),
                           .must_be_safe = square_bb(relative_square(ctx.us, F1)) |
                                           square_bb(relative_square(ctx.us, G1))};
    const Option queen_side{.right = white ? kWhiteQueenSide : kBlackQueenSide,
                            .king_to = relative_square(ctx.us, C1),
                            .must_be_empty = square_bb(relative_square(ctx.us, B1)) |
                                             square_bb(relative_square(ctx.us, C1)) |
                                             square_bb(relative_square(ctx.us, D1)),
                            .must_be_safe = square_bb(relative_square(ctx.us, C1)) |
                                            square_bb(relative_square(ctx.us, D1))};

    for (const Option& option : {king_side, queen_side}) {
        if ((rights & option.right) == kNoCastling || (ctx.occupied & option.must_be_empty) != 0) {
            continue;
        }
        bool safe = true;
        for (Bitboard squares = option.must_be_safe; squares != 0 && safe;) {
            safe = !attacked(ctx, pop_lsb(squares), occupied);
        }
        if (safe) {
            list.push_back(Move(ctx.king, option.king_to, MoveType::Castling));
        }
    }
}

}  // namespace

MoveList generate_legal_moves(const Position& pos) {
    MoveList list;
    generate_legal_moves(pos, list);
    return list;
}

void generate_legal_moves(const Position& pos, MoveList& list) {
    const Color us = pos.side_to_move();
    const Square king = pos.king_square(us);
    const Bitboard checkers = pos.checkers();
    const Context ctx{
        .pos = pos,
        .us = us,
        .king = king,
        .ours = pos.pieces(us),
        .theirs = pos.pieces(~us),
        .occupied = pos.pieces(),
        .pinned = pinned_pieces(pos, us, king),
        .target = checkers == 0 ? ~Bitboard{0} : between_bb(king, lsb(checkers)) | checkers};

    list.clear();
    generate_king_moves(ctx, list);
    if (more_than_one(checkers)) {
        return;  // Double check: only the king can move.
    }
    if (checkers == 0) {
        generate_castling(ctx, list);
    }
    generate_pawn_moves(ctx, list);
    generate_piece_moves(ctx, list);
}

std::optional<Move> parse_uci_move(const Position& pos, std::string_view text) {
    for (const Move move : generate_legal_moves(pos)) {
        if (move.to_uci() == text) {
            return move;
        }
    }
    return std::nullopt;
}

}  // namespace chess
