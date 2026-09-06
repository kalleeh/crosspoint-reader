#pragma once

// Game UI Constants
namespace GameConstants {
// Scoring
constexpr int SNAKE_SCORE_PER_FOOD = 10;
constexpr int SNAKE_SPEED_INCREASE = 10;

// Layout
constexpr int GAME_MARGIN = 15;
constexpr int CARD_PADDING = 5;
constexpr int CARD_INNER_MARGIN = 10;

// Chess piece values (centipawns)
constexpr int PAWN_VALUE = 100;
constexpr int KNIGHT_VALUE = 320;
constexpr int BISHOP_VALUE = 330;
constexpr int ROOK_VALUE = 500;
constexpr int QUEEN_VALUE = 900;
constexpr int KING_VALUE = 20000;

// Chess AI
constexpr int CHECKMATE_SCORE = 10;

// 2048
constexpr int TILE_2_PROBABILITY_PERCENT = 90;
constexpr int TILE_4_PROBABILITY_PERCENT = 10;

// UI positioning
constexpr int BATTERY_Y_OFFSET = 10;
constexpr int TITLE_Y_OFFSET = 15;
constexpr int TITLE_Y_OFFSET_LARGE = 20;
}  // namespace GameConstants
