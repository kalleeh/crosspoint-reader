#pragma once

#include <functional>
#include <vector>

#include "GameActivity.h"

class ChessActivity final : public GameActivity {
 private:
  enum Piece {
    EMPTY = 0,
    W_PAWN = 1,
    W_KNIGHT = 2,
    W_BISHOP = 3,
    W_ROOK = 4,
    W_QUEEN = 5,
    W_KING = 6,
    B_PAWN = -1,
    B_KNIGHT = -2,
    B_BISHOP = -3,
    B_ROOK = -4,
    B_QUEEN = -5,
    B_KING = -6
  };

  enum GameState { PLAYING, CHECK, CHECKMATE, STALEMATE, DRAW };

  struct Move {
    int fromX, fromY;
    int toX, toY;
    Piece capturedPiece;
    bool isPromotion;
    Piece promotionPiece;
  };

  Piece board[8][8];
  int cursorX, cursorY;
  int selectedX, selectedY;
  bool hasSelection;
  bool whiteTurn;
  GameState gameState;
  std::vector<Move> moveHistory;
  bool whiteKingMoved, blackKingMoved;
  bool whiteRookLeftMoved, whiteRookRightMoved;
  bool blackRookLeftMoved, blackRookRightMoved;
  const std::function<void()> onBack;

  // AI variables
  bool aiThinking;
  unsigned long aiStartTime;

  void resetGame();
  void render();
  bool isValidMove(int fromX, int fromY, int toX, int toY, bool checkKingSafety = true);
  void makeMove(int fromX, int fromY, int toX, int toY);
  void undoMove();
  bool isInCheck(bool white);
  bool isCheckmate(bool white);
  bool hasLegalMoves(bool white);
  int evaluateBoard();
  int minimax(int depth, int alpha, int beta, bool maximizing);
  Move findBestMove(int depth);
  void aiMove();
  std::vector<Move> getLegalMoves(bool white);
  void getPieceMoves(int x, int y, std::vector<Move>& moves);
  char pieceToChar(Piece p);
  void drawPiece(int x, int y, Piece piece);

 public:
  explicit ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : GameActivity("Chess", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return aiThinking; }
};
