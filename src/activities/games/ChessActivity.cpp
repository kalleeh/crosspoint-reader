#include "ChessActivity.h"

#include <GfxRenderer.h>

#include "../../ScreenComponents.h"
#include "../../fontIds.h"

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
  whiteKingMoved = blackKingMoved = false;
  whiteRookLeftMoved = whiteRookRightMoved = false;
  blackRookLeftMoved = blackRookRightMoved = false;
  aiThinking = false;
}

void ChessActivity::loop() {
  if (aiThinking) {
    aiMove();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
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
          if (isInCheck(!whiteTurn)) {
            if (isCheckmate(!whiteTurn)) {
              gameState = CHECKMATE;
            } else {
              gameState = CHECK;
            }
          } else if (!hasLegalMoves(!whiteTurn)) {
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
      break;
  }

  if (!validMove)
    return false;

  // Check if move leaves king in check
  if (checkKingSafety) {
    Piece originalTarget = board[toX][toY];
    board[toX][toY] = piece;
    board[fromX][fromY] = EMPTY;

    bool kingInCheck = isInCheck(isWhite);

    board[fromX][fromY] = piece;
    board[toX][toY] = originalTarget;

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

  board[toX][toY] = piece;
  board[fromX][fromY] = EMPTY;

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
  const int pieceValues[7] = {0, 100, 320, 330, 500, 900, 20000};  // empty, pawn, knight, bishop, rook, queen, king
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

  std::vector<Move> moves = getLegalMoves(!maximizing);
  if (moves.empty()) {
    if (isInCheck(!maximizing)) {
      return maximizing ? -30000 : 30000;  // Checkmate
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

      int eval = minimax(depth - 1, alpha, beta, false);

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

      int eval = minimax(depth - 1, alpha, beta, true);

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

  Move bestMove = findBestMove(3);  // Depth 3 for reasonable AI
  makeMove(bestMove.fromX, bestMove.fromY, bestMove.toX, bestMove.toY);

  whiteTurn = !whiteTurn;

  // Check game state
  if (isInCheck(!whiteTurn)) {
    if (isCheckmate(!whiteTurn)) {
      gameState = CHECKMATE;
    } else {
      gameState = CHECK;
    }
  } else if (!hasLegalMoves(!whiteTurn)) {
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

void ChessActivity::drawPiece(int x, int y, Piece piece) {
  char pieceChar[2];
  pieceChar[0] = pieceToChar(piece);
  pieceChar[1] = '\0';

  const int cellSize = 40;
  const int boardStartX = 80;
  const int boardStartY = 40;

  int screenX = boardStartX + x * cellSize + cellSize / 2 - 5;
  int screenY = boardStartY + y * cellSize + cellSize / 2 - 8;

  bool isWhite = (piece > 0);
  bool isDarkSquare = ((x + y) % 2 == 1);

  // Draw piece with appropriate color
  renderer.drawText(UI_12_FONT_ID, screenX, screenY, pieceChar, isWhite != isDarkSquare);
}

void ChessActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int cellSize = 40;
  const int boardStartX = 80;
  const int boardStartY = 40;

  // Title
  if (gameState == CHECKMATE) {
    const char* winner = whiteTurn ? "Black Wins!" : "White Wins!";
    renderer.drawCenteredText(UI_10_FONT_ID, 10, winner);
  } else if (gameState == STALEMATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 10, "Stalemate!");
  } else if (gameState == CHECK) {
    renderer.drawCenteredText(UI_10_FONT_ID, 10, "Check!");
  } else {
    const char* turn = whiteTurn ? "White's Turn" : "Black's Turn";
    renderer.drawCenteredText(UI_10_FONT_ID, 10, turn);
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

      // Highlight cursor
      if (x == cursorX && y == cursorY) {
        renderer.drawRect(screenX + 2, screenY + 2, cellSize - 4, cellSize - 4);
        renderer.drawRect(screenX + 4, screenY + 4, cellSize - 8, cellSize - 8);
      }

      // Highlight selected piece
      if (hasSelection && x == selectedX && y == selectedY) {
        renderer.fillRect(screenX + 6, screenY + 6, cellSize - 12, cellSize - 12);
      }

      // Draw piece
      Piece piece = board[x][y];
      if (piece != EMPTY) {
        drawPiece(x, y, piece);
      }
    }
  }

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "Select", "Move", "Move");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery indicator
  const auto batteryX = screenWidth - 25;
  ScreenComponents::drawBattery(renderer, batteryX, 10, false);

  renderer.displayBuffer();
}
