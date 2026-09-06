#include "../../DebugConfig.h"
#include "ChessActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "../../MappedInputManager.h"
#include "components/UITheme.h"
#include "../../fontIds.h"
#include "../../GameConstants.h"
#include <I18n.h>
#include <ForkI18n.h>

using namespace GameConstants;

void ChessActivity::onEnter() {
  Activity::onEnter();
  resetGame();
  render();
}

void ChessActivity::onExit() { Activity::onExit(); }

void ChessActivity::resetGame() {
  // Initialize board with standard chess setup
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      board[x][y] = EMPTY;
    }
  }

  // White pieces
  board[0][7] = W_ROOK;
  board[1][7] = W_KNIGHT;
  board[2][7] = W_BISHOP;
  board[3][7] = W_QUEEN;
  board[4][7] = W_KING;
  board[5][7] = W_BISHOP;
  board[6][7] = W_KNIGHT;
  board[7][7] = W_ROOK;
  for (int x = 0; x < 8; x++) {
    board[x][6] = W_PAWN;
  }

  // Black pieces
  board[0][0] = B_ROOK;
  board[1][0] = B_KNIGHT;
  board[2][0] = B_BISHOP;
  board[3][0] = B_QUEEN;
  board[4][0] = B_KING;
  board[5][0] = B_BISHOP;
  board[6][0] = B_KNIGHT;
  board[7][0] = B_ROOK;
  for (int x = 0; x < 8; x++) {
    board[x][1] = B_PAWN;
  }

  cursorX = 4;
  cursorY = 6;
  selectedX = -1;
  selectedY = -1;
  hasSelection = false;
  whiteTurn = true;
  gameState = PLAYING;
  moveHistory.clear();
  moveHistory.reserve(100);  // Reserve space for typical game length
  whiteKingMoved = blackKingMoved = false;
  whiteRookLeftMoved = whiteRookRightMoved = false;
  blackRookLeftMoved = blackRookRightMoved = false;
  enPassantX = -1;
  enPassantY = -1;
  aiThinking = false;
}

void ChessActivity::loop() {
  if (aiThinking) {
    aiMove();
    if (!aiThinking) {
      // AI just finished — consume any buttons pressed during thinking to prevent
      // them from immediately firing as player input on the next frame
      mappedInput.wasPressed(MappedInputManager::Button::Up);
      mappedInput.wasPressed(MappedInputManager::Button::Down);
      mappedInput.wasPressed(MappedInputManager::Button::Left);
      mappedInput.wasPressed(MappedInputManager::Button::Right);
      mappedInput.wasPressed(MappedInputManager::Button::Confirm);
      mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  // After game ends, Confirm starts a new game
  if (gameState == CHECKMATE || gameState == STALEMATE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      resetGame();
      render();
    }
    return;
  }

  bool needsRedraw = false;

  if (gameState == PLAYING || gameState == CHECK) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (cursorY > 0) {
        cursorY--;
        needsRedraw = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (cursorY < 7) {
        cursorY++;
        needsRedraw = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (cursorX > 0) {
        cursorX--;
        needsRedraw = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (cursorX < 7) {
        cursorX++;
        needsRedraw = true;
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!hasSelection) {
        // Select piece
        Piece piece = board[cursorX][cursorY];
        bool isWhitePiece = (piece > 0);
        bool isBlackPiece = (piece < 0);
        if ((whiteTurn && isWhitePiece) || (!whiteTurn && isBlackPiece)) {
          selectedX = cursorX;
          selectedY = cursorY;
          hasSelection = true;
          needsRedraw = true;
        }
      } else {
        // Try to move
        if (isValidMove(selectedX, selectedY, cursorX, cursorY)) {
          makeMove(selectedX, selectedY, cursorX, cursorY);
          hasSelection = false;
          whiteTurn = !whiteTurn;

          // Check game state
          if (isInCheck(whiteTurn)) {
            if (isCheckmate(whiteTurn)) {
              gameState = CHECKMATE;
            } else {
              gameState = CHECK;
            }
          } else if (!hasLegalMoves(whiteTurn)) {
            gameState = STALEMATE;
          } else {
            gameState = PLAYING;
          }

          needsRedraw = true;

          // Start AI thinking if black's turn
          if (gameState == PLAYING || gameState == CHECK) {
            if (!whiteTurn) {
              aiThinking = true;
              aiStartTime = millis();
            }
          }
        } else {
          // Cancel selection
          hasSelection = false;
          needsRedraw = true;
        }
      }
    }
  }

  if (needsRedraw) {
    render();
  }
}

bool ChessActivity::isValidMove(int fromX, int fromY, int toX, int toY, bool checkKingSafety) {
  if (fromX < 0 || fromX > 7 || fromY < 0 || fromY > 7 || toX < 0 || toX > 7 || toY < 0 || toY > 7) {
    return false;
  }

  Piece piece = board[fromX][fromY];
  if (piece == EMPTY)
    return false;

  Piece target = board[toX][toY];
  bool isWhite = (piece > 0);

  // Can't capture own piece
  if ((isWhite && target > 0) || (!isWhite && target < 0)) {
    return false;
  }

  int dx = toX - fromX;
  int dy = toY - fromY;
  int absDx = abs(dx);
  int absDy = abs(dy);

  bool validMove = false;

  switch (abs(piece)) {
    case W_PAWN: {
      int direction = isWhite ? -1 : 1;
      int startRow = isWhite ? 6 : 1;

      // Forward move
      if (dx == 0 && dy == direction && target == EMPTY) {
        validMove = true;
      }
      // Double forward from start
      else if (dx == 0 && dy == 2 * direction && fromY == startRow && target == EMPTY &&
               board[fromX][fromY + direction] == EMPTY) {
        validMove = true;
      }
      // Capture diagonal
      else if (absDx == 1 && dy == direction && target != EMPTY) {
        validMove = true;
      }
      // En passant capture
      else if (absDx == 1 && dy == direction && target == EMPTY &&
               toX == enPassantX && toY == enPassantY) {
        validMove = true;
      }
      break;
    }

    case W_KNIGHT:
      if ((absDx == 2 && absDy == 1) || (absDx == 1 && absDy == 2)) {
        validMove = true;
      }
      break;

    case W_BISHOP:
      if (absDx == absDy && absDx > 0) {
        // Check path is clear
        int stepX = (dx > 0) ? 1 : -1;
        int stepY = (dy > 0) ? 1 : -1;
        int x = fromX + stepX;
        int y = fromY + stepY;
        validMove = true;
        while (x != toX && y != toY) {
          if (board[x][y] != EMPTY) {
            validMove = false;
            break;
          }
          x += stepX;
          y += stepY;
        }
      }
      break;

    case W_ROOK:
      if ((dx == 0 && dy != 0) || (dx != 0 && dy == 0)) {
        // Check path is clear
        int stepX = (dx == 0) ? 0 : ((dx > 0) ? 1 : -1);
        int stepY = (dy == 0) ? 0 : ((dy > 0) ? 1 : -1);
        int x = fromX + stepX;
        int y = fromY + stepY;
        validMove = true;
        while (x != toX || y != toY) {
          if (board[x][y] != EMPTY) {
            validMove = false;
            break;
          }
          x += stepX;
          y += stepY;
        }
      }
      break;

    case W_QUEEN:
      // Queen = Rook + Bishop
      if ((dx == 0 && dy != 0) || (dx != 0 && dy == 0) || (absDx == absDy && absDx > 0)) {
        int stepX = (dx == 0) ? 0 : ((dx > 0) ? 1 : -1);
        int stepY = (dy == 0) ? 0 : ((dy > 0) ? 1 : -1);
        int x = fromX + stepX;
        int y = fromY + stepY;
        validMove = true;
        while (x != toX || y != toY) {
          if (board[x][y] != EMPTY) {
            validMove = false;
            break;
          }
          x += stepX;
          y += stepY;
        }
      }
      break;

    case W_KING:
      if (absDx <= 1 && absDy <= 1) {
        validMove = true;
      }
      // Castling: king moves exactly 2 squares horizontally from starting position
      else if (absDy == 0 && absDx == 2) {
        bool kMoved = isWhite ? whiteKingMoved : blackKingMoved;
        int row = isWhite ? 7 : 0;
        if (!kMoved && fromY == row && fromX == 4 && !isInCheck(isWhite)) {
          if (dx == 2) {  // Kingside
            bool rMoved = isWhite ? whiteRookRightMoved : blackRookRightMoved;
            if (!rMoved && board[5][row] == EMPTY && board[6][row] == EMPTY) {
              // Check king doesn't pass through f-file
              board[5][row] = piece;
              board[4][row] = EMPTY;
              bool passThroughCheck = isInCheck(isWhite);
              board[5][row] = EMPTY;
              board[4][row] = piece;
              if (!passThroughCheck) validMove = true;
            }
          } else {  // Queenside (dx == -2)
            bool rMoved = isWhite ? whiteRookLeftMoved : blackRookLeftMoved;
            if (!rMoved && board[1][row] == EMPTY && board[2][row] == EMPTY && board[3][row] == EMPTY) {
              // Check king doesn't pass through d-file
              board[3][row] = piece;
              board[4][row] = EMPTY;
              bool passThroughCheck = isInCheck(isWhite);
              board[3][row] = EMPTY;
              board[4][row] = piece;
              if (!passThroughCheck) validMove = true;
            }
          }
        }
      }
      break;
  }

  if (!validMove)
    return false;

  // Check if move leaves king in check
  if (checkKingSafety) {
    // For en passant, also temporarily remove the captured pawn
    bool isEP = (abs(piece) == 1 && toX == enPassantX && toY == enPassantY && board[toX][toY] == EMPTY);
    Piece epCaptured = EMPTY;
    if (isEP) {
      epCaptured = board[toX][fromY];
      board[toX][fromY] = EMPTY;
    }
    // For castling, also reposition the rook in the simulation
    bool isCastling = (abs(piece) == W_KING && abs(toX - fromX) == 2);
    Piece castleRook = EMPTY;
    int castleRookFromX = -1, castleRookToX = -1;
    if (isCastling) {
      int row = fromY;
      if (toX > fromX) {  // Kingside
        castleRookFromX = 7;
        castleRookToX = 5;
      } else {  // Queenside
        castleRookFromX = 0;
        castleRookToX = 3;
      }
      castleRook = board[castleRookFromX][row];
      board[castleRookToX][row] = castleRook;
      board[castleRookFromX][row] = EMPTY;
    }

    Piece originalTarget = board[toX][toY];
    board[toX][toY] = piece;
    board[fromX][fromY] = EMPTY;

    bool kingInCheck = isInCheck(isWhite);

    board[toX][toY] = originalTarget;
    board[fromX][fromY] = piece;

    if (isCastling) {
      board[castleRookFromX][fromY] = castleRook;
      board[castleRookToX][fromY] = EMPTY;
    }
    if (isEP) board[toX][fromY] = epCaptured;

    if (kingInCheck)
      return false;
  }

  return true;
}

void ChessActivity::makeMove(int fromX, int fromY, int toX, int toY) {
  Move move;
  move.fromX = fromX;
  move.fromY = fromY;
  move.toX = toX;
  move.toY = toY;
  move.capturedPiece = board[toX][toY];
  move.isPromotion = false;

  Piece piece = board[fromX][fromY];

  // Track castling rights
  if (abs(piece) == W_KING) {
    if (piece > 0)
      whiteKingMoved = true;
    else
      blackKingMoved = true;
  }
  if (abs(piece) == W_ROOK) {
    if (piece > 0) {
      if (fromX == 0)
        whiteRookLeftMoved = true;
      if (fromX == 7)
        whiteRookRightMoved = true;
    } else {
      if (fromX == 0)
        blackRookLeftMoved = true;
      if (fromX == 7)
        blackRookRightMoved = true;
    }
  }

  // Pawn promotion
  if (abs(piece) == W_PAWN) {
    if ((piece > 0 && toY == 0) || (piece < 0 && toY == 7)) {
      piece = (piece > 0) ? W_QUEEN : B_QUEEN;
      move.isPromotion = true;
      move.promotionPiece = piece;
    }
  }

  // En passant: remove the captured pawn (it sits at (toX, fromY))
  bool wasEnPassant = (abs(piece) == 1 && toX == enPassantX && toY == enPassantY);
  if (wasEnPassant) {
    board[toX][fromY] = EMPTY;
  }
  move.isEnPassant = wasEnPassant;

  board[toX][toY] = piece;
  board[fromX][fromY] = EMPTY;

  // Update en passant target for next move
  if (abs(board[toX][toY]) == 1 && abs(toY - fromY) == 2) {
    enPassantX = toX;
    enPassantY = (fromY + toY) / 2;  // Midpoint = passed-over square
  } else {
    enPassantX = -1;
    enPassantY = -1;
  }

  // Castling: also move the rook
  if (abs(piece) == 6 && abs(toX - fromX) == 2) {
    bool isWhiteP = piece > 0;
    int row = isWhiteP ? 7 : 0;
    if (toX == 6) {  // Kingside: rook h→f
      board[5][row] = isWhiteP ? W_ROOK : B_ROOK;
      board[7][row] = EMPTY;
    } else {  // Queenside: rook a→d
      board[3][row] = isWhiteP ? W_ROOK : B_ROOK;
      board[0][row] = EMPTY;
    }
  }

  moveHistory.push_back(move);
}

void ChessActivity::undoMove() {
  if (moveHistory.empty())
    return;

  Move move = moveHistory.back();
  moveHistory.pop_back();

  Piece piece = board[move.toX][move.toY];
  if (move.isPromotion) {
    piece = (piece > 0) ? W_PAWN : B_PAWN;
  }

  board[move.fromX][move.fromY] = piece;
  board[move.toX][move.toY] = move.capturedPiece;
  // Restore the pawn captured by en passant (it was at the side, not the destination)
  if (move.isEnPassant) {
    Piece capturedPawn = (piece > 0) ? B_PAWN : W_PAWN;
    board[move.toX][move.fromY] = capturedPawn;
  }
}

bool ChessActivity::isInCheck(bool white) {
  // Find king position
  int kingX = -1, kingY = -1;
  Piece king = white ? W_KING : B_KING;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      if (board[x][y] == king) {
        kingX = x;
        kingY = y;
        break;
      }
    }
    if (kingX != -1)
      break;
  }

  if (kingX == -1)
    return false;

  // Check if any opponent piece can attack the king
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      Piece piece = board[x][y];
      if (piece == EMPTY)
        continue;
      bool isOpponent = white ? (piece < 0) : (piece > 0);
      if (isOpponent) {
        if (isValidMove(x, y, kingX, kingY, false)) {
          return true;
        }
      }
    }
  }

  return false;
}

bool ChessActivity::hasLegalMoves(bool white) {
  for (int fromY = 0; fromY < 8; fromY++) {
    for (int fromX = 0; fromX < 8; fromX++) {
      Piece piece = board[fromX][fromY];
      if (piece == EMPTY)
        continue;
      bool isPieceWhite = (piece > 0);
      if (isPieceWhite != white)
        continue;

      for (int toY = 0; toY < 8; toY++) {
        for (int toX = 0; toX < 8; toX++) {
          if (isValidMove(fromX, fromY, toX, toY)) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool ChessActivity::isCheckmate(bool white) {
  return isInCheck(white) && !hasLegalMoves(white);
}

std::vector<ChessActivity::Move> ChessActivity::getLegalMoves(bool white) {
  std::vector<Move> moves;
  for (int fromY = 0; fromY < 8; fromY++) {
    for (int fromX = 0; fromX < 8; fromX++) {
      Piece piece = board[fromX][fromY];
      if (piece == EMPTY)
        continue;
      bool isPieceWhite = (piece > 0);
      if (isPieceWhite != white)
        continue;

      getPieceMoves(fromX, fromY, moves);
    }
  }
  return moves;
}

void ChessActivity::getPieceMoves(int x, int y, std::vector<Move>& moves) {
  for (int toY = 0; toY < 8; toY++) {
    for (int toX = 0; toX < 8; toX++) {
      if (isValidMove(x, y, toX, toY)) {
        Move move;
        move.fromX = x;
        move.fromY = y;
        move.toX = toX;
        move.toY = toY;
        move.capturedPiece = board[toX][toY];
        move.isPromotion = false;
        moves.push_back(move);
      }
    }
  }
}

int ChessActivity::evaluateBoard() {
  const int pieceValues[7] = {
    0,            // empty
    PAWN_VALUE,   // pawn
    KNIGHT_VALUE, // knight
    BISHOP_VALUE, // bishop
    ROOK_VALUE,   // rook
    QUEEN_VALUE,  // queen
    KING_VALUE    // king
  };
  int score = 0;

  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      Piece piece = board[x][y];
      if (piece != EMPTY) {
        int value = pieceValues[abs(piece)];
        score += (piece > 0) ? value : -value;
      }
    }
  }

  return score;
}

int ChessActivity::minimax(int depth, int alpha, int beta, bool maximizing) {
  if (depth == 0) {
    return evaluateBoard();
  }

  // maximizing == true means it is white's turn (evaluation is white-positive)
  std::vector<Move> moves = getLegalMoves(maximizing);
  if (moves.empty()) {
    if (isInCheck(maximizing)) {
      return maximizing ? -30000 : 30000;  // Side to move is checkmated
    }
    return 0;  // Stalemate
  }

  if (maximizing) {
    int maxEval = -40000;
    for (const Move& move : moves) {
      Piece originalTarget = board[move.toX][move.toY];
      Piece piece = board[move.fromX][move.fromY];
      board[move.toX][move.toY] = piece;
      board[move.fromX][move.fromY] = EMPTY;

      // Promotion: upgrade pawn to queen so evaluation sees the right piece value
      if (abs(piece) == 1 && (move.toY == 0 || move.toY == 7)) {
        board[move.toX][move.toY] = (piece > 0) ? W_QUEEN : B_QUEEN;
      }

      // En passant: remove the captured pawn at the side
      Piece epPawn = EMPTY;
      if (move.isEnPassant) {
        epPawn = board[move.toX][move.fromY];
        board[move.toX][move.fromY] = EMPTY;
      }

      // Castling: reposition the rook
      int castleRookFrom = -1, castleRookTo = -1;
      Piece castleRook = EMPTY;
      if (abs(piece) == W_KING && abs(move.toX - move.fromX) == 2) {
        castleRookFrom = (move.toX > move.fromX) ? 7 : 0;
        castleRookTo   = (move.toX > move.fromX) ? 5 : 3;
        castleRook = board[castleRookFrom][move.fromY];
        board[castleRookTo][move.fromY]   = castleRook;
        board[castleRookFrom][move.fromY] = EMPTY;
      }

      int eval = minimax(depth - 1, alpha, beta, false);

      // Restore in reverse order
      if (castleRookFrom != -1) {
        board[castleRookFrom][move.fromY] = castleRook;
        board[castleRookTo][move.fromY]   = EMPTY;
      }
      if (move.isEnPassant) {
        board[move.toX][move.fromY] = epPawn;
      }
      board[move.fromX][move.fromY] = piece;
      board[move.toX][move.toY] = originalTarget;

      maxEval = max(maxEval, eval);
      alpha = max(alpha, eval);
      if (beta <= alpha)
        break;
    }
    return maxEval;
  } else {
    int minEval = 40000;
    for (const Move& move : moves) {
      Piece originalTarget = board[move.toX][move.toY];
      Piece piece = board[move.fromX][move.fromY];
      board[move.toX][move.toY] = piece;
      board[move.fromX][move.fromY] = EMPTY;

      // Promotion: upgrade pawn to queen so evaluation sees the right piece value
      if (abs(piece) == 1 && (move.toY == 0 || move.toY == 7)) {
        board[move.toX][move.toY] = (piece > 0) ? W_QUEEN : B_QUEEN;
      }

      // En passant: remove the captured pawn at the side
      Piece epPawn = EMPTY;
      if (move.isEnPassant) {
        epPawn = board[move.toX][move.fromY];
        board[move.toX][move.fromY] = EMPTY;
      }

      // Castling: reposition the rook
      int castleRookFrom = -1, castleRookTo = -1;
      Piece castleRook = EMPTY;
      if (abs(piece) == W_KING && abs(move.toX - move.fromX) == 2) {
        castleRookFrom = (move.toX > move.fromX) ? 7 : 0;
        castleRookTo   = (move.toX > move.fromX) ? 5 : 3;
        castleRook = board[castleRookFrom][move.fromY];
        board[castleRookTo][move.fromY]   = castleRook;
        board[castleRookFrom][move.fromY] = EMPTY;
      }

      int eval = minimax(depth - 1, alpha, beta, true);

      // Restore in reverse order
      if (castleRookFrom != -1) {
        board[castleRookFrom][move.fromY] = castleRook;
        board[castleRookTo][move.fromY]   = EMPTY;
      }
      if (move.isEnPassant) {
        board[move.toX][move.fromY] = epPawn;
      }
      board[move.fromX][move.fromY] = piece;
      board[move.toX][move.toY] = originalTarget;

      minEval = min(minEval, eval);
      beta = min(beta, eval);
      if (beta <= alpha)
        break;
    }
    return minEval;
  }
}

ChessActivity::Move ChessActivity::findBestMove(int depth) {
  std::vector<Move> moves = getLegalMoves(false);  // Black's moves
  if (moves.empty()) {
    // Should not happen (aiThinking only set when game is not over), but guard anyway
    aiThinking = false;
    return Move{0, 0, 0, 0, EMPTY, false, EMPTY};
  }
  Move bestMove = moves[0];
  int bestValue = 40000;

  for (const Move& move : moves) {
    Piece originalTarget = board[move.toX][move.toY];
    Piece piece = board[move.fromX][move.fromY];
    board[move.toX][move.toY] = piece;
    board[move.fromX][move.fromY] = EMPTY;

    int moveValue = minimax(depth - 1, -40000, 40000, true);

    board[move.fromX][move.fromY] = piece;
    board[move.toX][move.toY] = originalTarget;

    if (moveValue < bestValue) {
      bestValue = moveValue;
      bestMove = move;
    }
  }

  return bestMove;
}

void ChessActivity::aiMove() {
  // Simple timing to make AI seem like it's thinking (min 500ms)
  unsigned long elapsed = millis() - aiStartTime;
  if (elapsed < 500) {
    return;
  }

  Move bestMove = findBestMove(1);  // Depth 1 for ESP32 performance (was 3)
  makeMove(bestMove.fromX, bestMove.fromY, bestMove.toX, bestMove.toY);

  whiteTurn = !whiteTurn;

  // Check game state
  if (isInCheck(whiteTurn)) {
    if (isCheckmate(whiteTurn)) {
      gameState = CHECKMATE;
    } else {
      gameState = CHECK;
    }
  } else if (!hasLegalMoves(whiteTurn)) {
    gameState = STALEMATE;
  } else {
    gameState = PLAYING;
  }

  aiThinking = false;
  render();
}

char ChessActivity::pieceToChar(Piece p) {
  switch (p) {
    case W_PAWN:
      return 'P';
    case W_KNIGHT:
      return 'N';
    case W_BISHOP:
      return 'B';
    case W_ROOK:
      return 'R';
    case W_QUEEN:
      return 'Q';
    case W_KING:
      return 'K';
    case B_PAWN:
      return 'p';
    case B_KNIGHT:
      return 'n';
    case B_BISHOP:
      return 'b';
    case B_ROOK:
      return 'r';
    case B_QUEEN:
      return 'q';
    case B_KING:
      return 'k';
    default:
      return ' ';
  }
}

void ChessActivity::drawPiece(int x, int y, Piece piece, int cellSize, int boardStartX, int boardStartY) {
  const int screenX = boardStartX + x * cellSize;
  const int screenY = boardStartY + y * cellSize;
  const bool isWhite = (piece > 0);
  const int pieceType = abs(piece);
  
  // Use 70% of cell for piece
  const int pieceSize = (cellSize * 7) / 10;
  const int offset = (cellSize - pieceSize) / 2;
  const int px = screenX + offset;
  const int py = screenY + offset;
  const int w = pieceSize;
  const int h = pieceSize;
  
  // Helper to draw circle (approximation)
  auto drawCircle = [&](int cx, int cy, int r, bool filled, bool white) {
    for (int dy = -r; dy <= r; dy++) {
      int dx = (int)sqrt(r*r - dy*dy);
      if (filled) {
        renderer.drawLine(cx - dx, cy + dy, cx + dx, cy + dy, white);
      } else {
        renderer.drawPixel(cx - dx, cy + dy, white);
        renderer.drawPixel(cx + dx, cy + dy, white);
      }
    }
  };
  
  switch (pieceType) {
    case W_KING: {
      // Base
      renderer.fillRect(px + w/4, py + h*4/5, w/2, h/6, !isWhite);
      if (isWhite) renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6);
      // Body
      renderer.fillRect(px + w/3, py + h/2, w/3, h/3, !isWhite);
      if (isWhite) renderer.drawRect(px + w/3, py + h/2, w/3, h/3);
      // Crown points
      for (int i = 0; i < 5; i++) {
        int cpx = px + w/4 + i*w/6;
        renderer.fillRect(cpx, py + h/3, w/12, h/6, !isWhite);
      }
      // Cross
      renderer.fillRect(px + w/2 - 1, py + h/10, 2, h/5, !isWhite);
      renderer.fillRect(px + w/2 - w/12, py + h/6, w/6, 2, !isWhite);
      if (!isWhite) {
        renderer.drawRect(px + w/3, py + h/2, w/3, h/3, false);
        renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6, false);
      }
      break;
    }
    
    case W_QUEEN: {
      // Base
      renderer.fillRect(px + w/4, py + h*4/5, w/2, h/6, !isWhite);
      if (isWhite) renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6);
      // Body
      renderer.fillRect(px + w/3, py + h/2, w/3, h/3, !isWhite);
      if (isWhite) renderer.drawRect(px + w/3, py + h/2, w/3, h/3);
      // Crown with 5 spheres
      for (int i = 0; i < 5; i++) {
        int cpx = px + w/5 + i*w/6;
        drawCircle(cpx, py + h/4, w/12, true, !isWhite);
        if (isWhite) drawCircle(cpx, py + h/4, w/12, false, true);
      }
      if (!isWhite) {
        renderer.drawRect(px + w/3, py + h/2, w/3, h/3, false);
        renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6, false);
      }
      break;
    }
    
    case W_BISHOP: {
      // Base
      renderer.fillRect(px + w/4, py + h*4/5, w/2, h/6, !isWhite);
      if (isWhite) renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6);
      // Body (trapezoid)
      for (int i = 0; i < h/3; i++) {
        int lw = w/3 - (i * w/12) / (h/3);
        renderer.drawLine(px + w/2 - lw/2, py + h/2 + i, px + w/2 + lw/2, py + h/2 + i, !isWhite);
      }
      // Point (triangle)
      for (int i = 0; i < h/4; i++) {
        renderer.drawLine(px + w/2 - i/2, py + h/4 + i, px + w/2 + i/2, py + h/4 + i, !isWhite);
      }
      // Slot
      renderer.fillRect(px + w/2 - 1, py + h/6, 2, h/8, isWhite);
      // Circle on top
      drawCircle(px + w/2, py + h/8, w/12, true, !isWhite);
      if (isWhite) drawCircle(px + w/2, py + h/8, w/12, false, true);
      if (!isWhite) {
        renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6, false);
        // Outline
        for (int i = 0; i < h/3; i++) {
          int lw = w/3 - (i * w/12) / (h/3);
          renderer.drawPixel(px + w/2 - lw/2, py + h/2 + i, false);
          renderer.drawPixel(px + w/2 + lw/2, py + h/2 + i, false);
        }
      }
      break;
    }
    
    case W_KNIGHT: {
      // Base
      renderer.fillRect(px + w/4, py + h*4/5, w/2, h/6, !isWhite);
      if (isWhite) renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6);
      // Neck
      renderer.fillRect(px + w/3, py + h/2, w/5, h/3, !isWhite);
      // Head (curved)
      for (int i = 0; i < h/4; i++) {
        int hw = w/4 + (i * w/8) / (h/4);
        renderer.drawLine(px + w/2 - hw/2, py + h/4 + i, px + w/2 + hw/2, py + h/4 + i, !isWhite);
      }
      // Snout
      renderer.fillRect(px + w/2, py + h/3, w/5, h/8, !isWhite);
      // Ear
      renderer.fillRect(px + w/2 - w/10, py + h/6, w/8, h/10, !isWhite);
      // Eye
      renderer.fillRect(px + w/2 - w/16, py + h/3, 2, 2, isWhite);
      if (!isWhite) {
        renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6, false);
        // Outline
        renderer.drawRect(px + w/3, py + h/2, w/5, h/3, false);
        renderer.drawRect(px + w/2, py + h/3, w/5, h/8, false);
      }
      break;
    }
    
    case W_ROOK: {
      // Base
      renderer.fillRect(px + w/4, py + h*4/5, w/2, h/6, !isWhite);
      if (isWhite) renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6);
      // Tower body
      renderer.fillRect(px + w/3, py + h/3, w/3, h/2, !isWhite);
      if (isWhite) renderer.drawRect(px + w/3, py + h/3, w/3, h/2);
      // Crenellations (3 notches)
      const int crenW = w/10;
      const int crenH = h/8;
      renderer.fillRect(px + w/3, py + h/3, crenW, crenH, !isWhite);
      renderer.fillRect(px + w/2 - crenW/2, py + h/3, crenW, crenH, !isWhite);
      renderer.fillRect(px + w*2/3 - crenW, py + h/3, crenW, crenH, !isWhite);
      if (!isWhite) {
        renderer.drawRect(px + w/3, py + h/3, w/3, h/2, false);
        renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6, false);
      }
      break;
    }
    
    case W_PAWN: {
      // Base
      renderer.fillRect(px + w/4, py + h*4/5, w/2, h/6, !isWhite);
      if (isWhite) renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6);
      // Body (smooth trapezoid)
      for (int i = 0; i < h/2; i++) {
        int bw = w/4 + (i * w/8) / (h/2);
        renderer.drawLine(px + w/2 - bw/2, py + h/3 + i, px + w/2 + bw/2, py + h/3 + i, !isWhite);
      }
      // Head (circle)
      drawCircle(px + w/2, py + h/4, w/6, true, !isWhite);
      if (isWhite) {
        drawCircle(px + w/2, py + h/4, w/6, false, true);
        renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6);
      } else {
        renderer.drawRect(px + w/4, py + h*4/5, w/2, h/6, false);
        // Outline for body
        for (int i = 0; i < h/2; i++) {
          int bw = w/4 + (i * w/8) / (h/2);
          renderer.drawPixel(px + w/2 - bw/2, py + h/3 + i, false);
          renderer.drawPixel(px + w/2 + bw/2, py + h/3 + i, false);
        }
      }
      break;
    }
  }
}

void ChessActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  
  // Calculate optimal board size - use full width
  const int topMargin = 60;  // More space for title
  const int bottomMargin = 50;
  const int availableHeight = screenHeight - topMargin - bottomMargin;
  const int availableWidth = screenWidth - 40;
  
  const int cellSizeByWidth = availableWidth / 8;
  const int cellSizeByHeight = availableHeight / 8;
  const int cellSize = (cellSizeByWidth < cellSizeByHeight) ? cellSizeByWidth : cellSizeByHeight;
  
  const int boardSize = cellSize * 8;
  const int boardStartX = (screenWidth - boardSize) / 2;
  const int boardStartY = topMargin;

  // Title at top — show AI thinking indicator when AI is computing
  if (aiThinking) {
    renderer.drawCenteredText(UI_10_FONT_ID, 15, fork_tr(STR_CHESS_AI_THINKING));
  } else if (gameState == CHECKMATE) {
    const char* winner = whiteTurn ? fork_tr(STR_CHESS_BLACK_WINS) : fork_tr(STR_CHESS_WHITE_WINS);
    renderer.drawCenteredText(UI_10_FONT_ID, 15, winner);
  } else if (gameState == STALEMATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 15, fork_tr(STR_CHESS_STALEMATE));
  } else if (gameState == CHECK) {
    renderer.drawCenteredText(UI_10_FONT_ID, 15, fork_tr(STR_CHESS_CHECK));
  } else {
    const char* turn = whiteTurn ? fork_tr(STR_CHESS_WHITE_TURN) : fork_tr(STR_CHESS_BLACK_TURN);
    renderer.drawCenteredText(UI_10_FONT_ID, 15, turn);
  }

  // Draw board
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      int screenX = boardStartX + x * cellSize;
      int screenY = boardStartY + y * cellSize;

      // Draw square
      bool isDark = (x + y) % 2 == 1;
      if (isDark) {
        renderer.fillRect(screenX, screenY, cellSize, cellSize);
      } else {
        renderer.drawRect(screenX, screenY, cellSize, cellSize);
      }

      // Highlight cursor (inverted on dark squares for visibility)
      if (x == cursorX && y == cursorY) {
        renderer.drawRect(screenX + 2, screenY + 2, cellSize - 4, cellSize - 4, !isDark);
        renderer.drawRect(screenX + 4, screenY + 4, cellSize - 8, cellSize - 8, !isDark);
      }

      // Highlight selected piece (inverted on dark squares)
      if (hasSelection && x == selectedX && y == selectedY) {
        renderer.fillRect(screenX + 6, screenY + 6, cellSize - 12, cellSize - 12, !isDark);
      }

      // Draw piece
      Piece piece = board[x][y];
      if (piece != EMPTY) {
        drawPiece(x, y, piece, cellSize, boardStartX, boardStartY);
      }
    }
  }

  // Button hints — show restart option when game is over
  const bool gameOver = (gameState == CHECKMATE || gameState == STALEMATE);
  const auto labels = gameOver
    ? mappedInput.mapLabels(tr(STR_BACK), fork_tr(STR_GAME_RESTART), "", "")
    : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), fork_tr(STR_BTN_MOVE), fork_tr(STR_BTN_MOVE));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery indicator at top right
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawBatteryLeft(renderer, Rect{screenWidth - 25, 10, metrics.batteryWidth, metrics.batteryHeight}, false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
