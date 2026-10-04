#include "position.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "magic.hpp"
#include "util.hpp"
#include "zobrist.hpp"

namespace chess {
namespace {

constexpr std::string_view kPieceChars = "PNBRQKpnbrqk";

std::optional<Piece> piece_from_char(char symbol) {
    const auto index = kPieceChars.find(symbol);
    if (index == std::string_view::npos) {
        return std::nullopt;
    }
    return static_cast<Piece>(index);
}

char piece_to_char(Piece piece) {
    return piece == NoPiece ? '.' : kPieceChars[piece];
}

// Castling rights that survive a move touching each square: moving the king or a rook, or
// capturing a rook on its original square, loses the corresponding rights.
consteval std::array<CastlingRights, kSquareCount> make_castling_masks() {
    std::array<CastlingRights, kSquareCount> masks{};
    masks.fill(kAllCastling);
    masks[A1] = static_cast<CastlingRights>(kAllCastling & ~kWhiteQueenSide);
    masks[H1] = static_cast<CastlingRights>(kAllCastling & ~kWhiteKingSide);
    masks[E1] = static_cast<CastlingRights>(kAllCastling & ~(kWhiteKingSide | kWhiteQueenSide));
    masks[A8] = static_cast<CastlingRights>(kAllCastling & ~kBlackQueenSide);
    masks[H8] = static_cast<CastlingRights>(kAllCastling & ~kBlackKingSide);
    masks[E8] = static_cast<CastlingRights>(kAllCastling & ~(kBlackKingSide | kBlackQueenSide));
    return masks;
}

constexpr auto kCastlingMasks = make_castling_masks();

// Rook origin and destination for a castling move, from the king's destination square.
struct CastlingRook {
    Square from;
    Square to;
};

constexpr CastlingRook castling_rook(Square king_to) noexcept {
    const bool king_side = file_of(king_to) == 6;
    const int rank = rank_of(king_to);
    return king_side ? CastlingRook{.from = make_square(7, rank), .to = make_square(5, rank)}
                     : CastlingRook{.from = make_square(0, rank), .to = make_square(3, rank)};
}

std::optional<Square> parse_square(std::string_view text) {
    if (text.size() != 2 || text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8') {
        return std::nullopt;
    }
    return make_square(text[0] - 'a', text[1] - '1');
}

std::vector<std::string_view> split_fields(std::string_view text) {
    std::vector<std::string_view> fields;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto start = text.find_first_not_of(' ', pos);
        if (start == std::string_view::npos) {
            break;
        }
        auto end = text.find(' ', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        fields.push_back(text.substr(start, end - start));
        pos = end;
    }
    return fields;
}

}  // namespace

Position::Position() : Position(EmptyTag{}) {
    [[maybe_unused]] const bool valid = parse_fen(kStartFen);
    assert(valid);
}

Position::Position(EmptyTag /*tag*/) noexcept {
    board_.fill(NoPiece);
}

std::optional<Position> Position::from_fen(std::string_view fen) {
    Position pos{EmptyTag{}};
    if (!pos.parse_fen(fen)) {
        return std::nullopt;
    }
    return pos;
}

bool Position::parse_fen(std::string_view fen) {
    const auto fields = split_fields(fen);
    if (fields.size() < 4 || fields.size() > 6) {
        return false;
    }

    // 1. Piece placement, from rank 8 down to rank 1.
    int rank = 7;
    int file = 0;
    for (const char symbol : fields[0]) {
        if (symbol == '/') {
            if (file != 8 || rank == 0) {
                return false;
            }
            --rank;
            file = 0;
        } else if (symbol >= '1' && symbol <= '8') {
            file += symbol - '0';
            if (file > 8) {
                return false;
            }
        } else {
            const auto piece = piece_from_char(symbol);
            if (!piece.has_value() || file >= 8) {
                return false;
            }
            put_piece(*piece, make_square(file, rank));
            ++file;
        }
    }
    if (rank != 0 || file != 8) {
        return false;
    }

    // 2. Side to move.
    if (fields[1] == "w") {
        side_to_move_ = White;
    } else if (fields[1] == "b") {
        side_to_move_ = Black;
        key_ ^= zobrist::side_to_move();
    } else {
        return false;
    }

    // 3. Castling rights. Rights whose king or rook is not on its original square are dropped.
    castling_ = kNoCastling;
    if (fields[2] != "-") {
        for (const char symbol : fields[2]) {
            switch (symbol) {
                case 'K':
                    castling_ |= kWhiteKingSide;
                    break;
                case 'Q':
                    castling_ |= kWhiteQueenSide;
                    break;
                case 'k':
                    castling_ |= kBlackKingSide;
                    break;
                case 'q':
                    castling_ |= kBlackQueenSide;
                    break;
                default:
                    return false;
            }
        }
    }
    const auto keep_if = [this](CastlingRights right, Square king, Square rook, Color color) {
        if (piece_on(king) != make_piece(color, King) ||
            piece_on(rook) != make_piece(color, Rook)) {
            castling_ = static_cast<CastlingRights>(castling_ & ~right);
        }
    };
    keep_if(kWhiteKingSide, E1, H1, White);
    keep_if(kWhiteQueenSide, E1, A1, White);
    keep_if(kBlackKingSide, E8, H8, Black);
    keep_if(kBlackQueenSide, E8, A8, Black);
    key_ ^= zobrist::castling(castling_);

    // 4. En passant target square.
    en_passant_ = NoSquare;
    if (fields[3] != "-") {
        const auto square = parse_square(fields[3]);
        if (!square || relative_rank(side_to_move_, *square) != 5) {
            return false;
        }
        // Only meaningful if the pawn that just double-pushed is really there.
        const Square pushed = offset(*square, -pawn_push(side_to_move_));
        if (piece_on(pushed) == make_piece(~side_to_move_, Pawn) && piece_on(*square) == NoPiece) {
            set_en_passant(*square);
        }
    }

    // 5 and 6. Move counters, optional.
    halfmove_clock_ = 0;
    fullmove_number_ = 1;
    if (fields.size() > 4) {
        const auto halfmove = parse_int(fields[4]);
        if (!halfmove || *halfmove < 0) {
            return false;
        }
        halfmove_clock_ = *halfmove;
    }
    if (fields.size() > 5) {
        const auto fullmove = parse_int(fields[5]);
        if (!fullmove || *fullmove < 1) {
            return false;
        }
        fullmove_number_ = *fullmove;
    }

    return is_consistent();
}

bool Position::is_consistent() const {
    if (popcount(pieces(White, King)) != 1 || popcount(pieces(Black, King)) != 1) {
        return false;
    }
    if ((pieces(Pawn) & (kRank1 | kRank8)) != 0) {
        return false;
    }
    // The side that just moved cannot have left its king in check.
    const Color them = ~side_to_move_;
    return (attackers_to(king_square(them)) & pieces(side_to_move_)) == 0;
}

std::string Position::fen() const {
    std::string result;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = piece_on(make_square(file, rank));
            if (piece == NoPiece) {
                ++empty;
                continue;
            }
            if (empty > 0) {
                result += static_cast<char>('0' + empty);
                empty = 0;
            }
            result += piece_to_char(piece);
        }
        if (empty > 0) {
            result += static_cast<char>('0' + empty);
        }
        if (rank > 0) {
            result += '/';
        }
    }

    result += side_to_move_ == White ? " w " : " b ";

    if (castling_ == kNoCastling) {
        result += '-';
    } else {
        for (const auto& [right, symbol] :
             {std::pair{kWhiteKingSide, 'K'}, std::pair{kWhiteQueenSide, 'Q'},
              std::pair{kBlackKingSide, 'k'}, std::pair{kBlackQueenSide, 'q'}}) {
            if ((castling_ & right) != kNoCastling) {
                result += symbol;
            }
        }
    }

    result += ' ';
    result += en_passant_ == NoSquare ? "-" : square_name(en_passant_);
    result += ' ' + std::to_string(halfmove_clock_) + ' ' + std::to_string(fullmove_number_);
    return result;
}

std::string Position::pretty() const {
    std::ostringstream out;
    for (int rank = 7; rank >= 0; --rank) {
        out << (rank + 1) << ' ';
        for (int file = 0; file < 8; ++file) {
            out << ' ' << piece_to_char(piece_on(make_square(file, rank)));
        }
        out << '\n';
    }
    out << "   a b c d e f g h\n\n"
        << "Fen: " << fen() << '\n'
        << "Key: " << std::hex << std::uppercase << key_ << '\n';
    return out.str();
}

Bitboard Position::attackers_to(Square square, Bitboard occupancy) const {
    return (pawn_attacks(Black, square) & pieces(White, Pawn)) |
           (pawn_attacks(White, square) & pieces(Black, Pawn)) |
           (knight_attacks(square) & pieces(Knight)) | (king_attacks(square) & pieces(King)) |
           (rook_attacks(square, occupancy) & (pieces(Rook) | pieces(Queen))) |
           (bishop_attacks(square, occupancy) & (pieces(Bishop) | pieces(Queen)));
}

Bitboard Position::checkers() const {
    return attackers_to(king_square(side_to_move_)) & pieces(~side_to_move_);
}

void Position::put_piece(Piece piece, Square square) noexcept {
    board_[square] = piece;
    by_color_[color_of(piece)] |= square_bb(square);
    by_type_[type_of(piece)] |= square_bb(square);
    key_ ^= zobrist::piece_square(piece, square);
}

void Position::remove_piece(Square square) noexcept {
    const Piece piece = board_[square];
    board_[square] = NoPiece;
    by_color_[color_of(piece)] ^= square_bb(square);
    by_type_[type_of(piece)] ^= square_bb(square);
    key_ ^= zobrist::piece_square(piece, square);
}

void Position::move_piece(Square from, Square to) noexcept {
    const Piece piece = board_[from];
    const Bitboard from_to = square_bb(from) | square_bb(to);
    board_[from] = NoPiece;
    board_[to] = piece;
    by_color_[color_of(piece)] ^= from_to;
    by_type_[type_of(piece)] ^= from_to;
    key_ ^= zobrist::piece_square(piece, from) ^ zobrist::piece_square(piece, to);
}

void Position::set_en_passant(Square square) noexcept {
    // Recording the square only when a capture is pseudo-legal keeps transposed positions on
    // the same key.
    if ((pawn_attacks(~side_to_move_, square) & pieces(side_to_move_, Pawn)) != 0) {
        en_passant_ = square;
        key_ ^= zobrist::en_passant(square);
    }
}

void Position::make_move(Move move) {
    history_.push_back(UndoInfo{.move = move,
                                .captured = NoPiece,
                                .castling = castling_,
                                .en_passant = en_passant_,
                                .halfmove_clock = halfmove_clock_,
                                .key = key_});
    UndoInfo& undo = history_.back();

    const Color us = side_to_move_;
    const Square from = move.from();
    const Square to = move.to();

    if (en_passant_ != NoSquare) {
        key_ ^= zobrist::en_passant(en_passant_);
        en_passant_ = NoSquare;
    }
    ++halfmove_clock_;

    switch (move.type()) {
        case MoveType::Castling: {
            const auto rook = castling_rook(to);
            move_piece(from, to);
            move_piece(rook.from, rook.to);
            break;
        }
        case MoveType::EnPassant: {
            const Square captured_square = offset(to, -pawn_push(us));
            undo.captured = piece_on(captured_square);
            remove_piece(captured_square);
            move_piece(from, to);
            halfmove_clock_ = 0;
            break;
        }
        case MoveType::Normal:
        case MoveType::Promotion: {
            if (piece_on(to) != NoPiece) {
                undo.captured = piece_on(to);
                remove_piece(to);
                halfmove_clock_ = 0;
            }
            move_piece(from, to);
            if (type_of(piece_on(to)) == Pawn) {
                halfmove_clock_ = 0;
                if (move.type() == MoveType::Promotion) {
                    remove_piece(to);
                    put_piece(make_piece(us, move.promotion()), to);
                }
            }
            break;
        }
    }

    const CastlingRights castling = castling_ & kCastlingMasks[from] & kCastlingMasks[to];
    if (castling != castling_) {
        key_ ^= zobrist::castling(castling_) ^ zobrist::castling(castling);
        castling_ = castling;
    }

    if (us == Black) {
        ++fullmove_number_;
    }
    side_to_move_ = ~us;
    key_ ^= zobrist::side_to_move();

    // A double push may enable an en passant capture for the side now to move.
    if (type_of(piece_on(to)) == Pawn && (from ^ to) == 16) {
        set_en_passant(offset(from, pawn_push(us)));
    }
}

bool Position::is_repetition() const noexcept {
    // A position can only repeat after both sides have made at least two moves, and never across
    // an irreversible move, which resets the halfmove clock.
    // A null move is not a real move, so positions before one are not repetitions either.
    const std::size_t plies = history_.size();
    const std::size_t reversible = std::min(plies, static_cast<std::size_t>(halfmove_clock_));
    for (std::size_t back = 1; back <= reversible; ++back) {
        const UndoInfo& undo = history_[plies - back];
        if (undo.move.is_null()) {
            return false;
        }
        if (back >= 4 && back % 2 == 0 && undo.key == key_) {
            return true;
        }
    }
    return false;
}

void Position::make_null_move() {
    history_.push_back(UndoInfo{.move = Move::null(),
                                .captured = NoPiece,
                                .castling = castling_,
                                .en_passant = en_passant_,
                                .halfmove_clock = halfmove_clock_,
                                .key = key_});
    if (en_passant_ != NoSquare) {
        key_ ^= zobrist::en_passant(en_passant_);
        en_passant_ = NoSquare;
    }
    ++halfmove_clock_;
    if (side_to_move_ == Black) {
        ++fullmove_number_;
    }
    side_to_move_ = ~side_to_move_;
    key_ ^= zobrist::side_to_move();
}

void Position::unmake_null_move() {
    const UndoInfo undo = history_.back();
    history_.pop_back();
    side_to_move_ = ~side_to_move_;
    if (side_to_move_ == Black) {
        --fullmove_number_;
    }
    en_passant_ = undo.en_passant;
    halfmove_clock_ = undo.halfmove_clock;
    key_ = undo.key;
}

void Position::unmake_move() {
    const UndoInfo undo = history_.back();
    history_.pop_back();

    side_to_move_ = ~side_to_move_;
    const Color us = side_to_move_;
    if (us == Black) {
        --fullmove_number_;
    }

    const Move move = undo.move;
    const Square from = move.from();
    const Square to = move.to();

    switch (move.type()) {
        case MoveType::Castling: {
            const auto rook = castling_rook(to);
            move_piece(rook.to, rook.from);
            move_piece(to, from);
            break;
        }
        case MoveType::EnPassant:
            move_piece(to, from);
            put_piece(undo.captured, offset(to, -pawn_push(us)));
            break;
        case MoveType::Promotion:
            remove_piece(to);
            put_piece(make_piece(us, Pawn), from);
            if (undo.captured != NoPiece) {
                put_piece(undo.captured, to);
            }
            break;
        case MoveType::Normal:
            move_piece(to, from);
            if (undo.captured != NoPiece) {
                put_piece(undo.captured, to);
            }
            break;
    }

    castling_ = undo.castling;
    en_passant_ = undo.en_passant;
    halfmove_clock_ = undo.halfmove_clock;
    key_ = undo.key;
}

Key Position::compute_key() const {
    Key key = 0;
    for (Bitboard occupied = pieces(); occupied != 0;) {
        const Square square = pop_lsb(occupied);
        key ^= zobrist::piece_square(piece_on(square), square);
    }
    key ^= zobrist::castling(castling_);
    if (en_passant_ != NoSquare) {
        key ^= zobrist::en_passant(en_passant_);
    }
    if (side_to_move_ == Black) {
        key ^= zobrist::side_to_move();
    }
    return key;
}

}  // namespace chess
