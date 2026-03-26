#include <tamtypes.h>
#include <kernel.h>
#include <graph.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "include/common.h"
#include "include/pad.h"
#include "include/renderman.h"
#include "include/dialogs.h"
#include "include/themes.h"
#include "include/fntsys.h"
#include "include/tetris.h"

extern GSGLOBAL *gsGlobal;
extern theme_t *gTheme;

#define TETRIS_COLS 10
#define TETRIS_ROWS 20

static const u32 pieceColors[] = {
    GS_SETREG_RGBA(0x40, 0x40, 0x40, 0x80), // empty/ghost
    GS_SETREG_RGBA(0x2b, 0xa1, 0xc2, 0x80),
    GS_SETREG_RGBA(0xf7, 0xd0, 0x38, 0x80),
    GS_SETREG_RGBA(0xd9, 0x53, 0x4f, 0x80),
    GS_SETREG_RGBA(0x7c, 0xc5, 0x4f, 0x80),
    GS_SETREG_RGBA(0xc3, 0x7b, 0xd0, 0x80),
    GS_SETREG_RGBA(0xff, 0x9f, 0x43, 0x80),
    GS_SETREG_RGBA(0x6a, 0x6a, 0x6a, 0x80)};

// Tetromino definitions: 7 pieces, 4 rotations, 4 cells (x,y)
static const char pieces[7][4][4][2] = {
    {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{1, -1}, {1, 0}, {1, 1}, {1, 2}}, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, {{2, -1}, {2, 0}, {2, 1}, {2, 2}}}, // I
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {1, 1}, {1, 2}}, {{0, 1}, {1, 1}, {2, 1}, {2, 2}}, {{1, 0}, {1, 1}, {0, 2}, {1, 2}}},   // J
    {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}, {{1, 0}, {1, 1}, {1, 2}, {2, 2}}, {{0, 1}, {1, 1}, {2, 1}, {0, 2}}, {{0, 0}, {1, 0}, {1, 1}, {1, 2}}},   // L
    {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {1, 1}, {2, 1}}},   // O
    {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}, {{1, 0}, {1, 1}, {2, 1}, {2, 2}}, {{1, 1}, {2, 1}, {0, 2}, {1, 2}}, {{0, 0}, {0, 1}, {1, 1}, {1, 2}}},   // S
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}, {{2, 0}, {1, 1}, {2, 1}, {1, 2}}, {{0, 1}, {1, 1}, {1, 2}, {2, 2}}, {{1, 0}, {0, 1}, {1, 1}, {0, 2}}},   // Z
    {{{1, 0}, {0, 1}, {1, 1}, {2, 1}}, {{1, 0}, {1, 1}, {2, 1}, {1, 2}}, {{0, 1}, {1, 1}, {2, 1}, {1, 2}}, {{1, 0}, {0, 1}, {1, 1}, {1, 2}}}};  // T

static u8 board[TETRIS_ROWS][TETRIS_COLS];
static int blockSize;
static int originX;
static int originY;
static int curPiece, curRot, curX, curY;
static int nextPiece;
static int dropCounter;

static int randPiece(void)
{
    return rand() % 7;
}

static int canPlace(int piece, int rot, int x, int y)
{
    for (int i = 0; i < 4; i++) {
        int px = x + pieces[piece][rot][i][0];
        int py = y + pieces[piece][rot][i][1];
        if (px < 0 || px >= TETRIS_COLS || py < 0 || py >= TETRIS_ROWS)
            return 0;
        if (board[py][px])
            return 0;
    }
    return 1;
}

static void placePiece(void)
{
    for (int i = 0; i < 4; i++) {
        int px = curX + pieces[curPiece][curRot][i][0];
        int py = curY + pieces[curPiece][curRot][i][1];
        if (py >= 0 && py < TETRIS_ROWS && px >= 0 && px < TETRIS_COLS)
            board[py][px] = curPiece + 1;
    }
}

static void clearLines(void)
{
    for (int y = TETRIS_ROWS - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < TETRIS_COLS; x++) {
            if (!board[y][x]) {
                full = 0;
                break;
            }
        }
        if (full) {
            for (int yy = y; yy > 0; yy--)
                memcpy(board[yy], board[yy - 1], TETRIS_COLS);
            memset(board[0], 0, TETRIS_COLS);
            y++; // recheck this row after shift
        }
    }
}

static void spawnPiece(void)
{
    curPiece = nextPiece;
    nextPiece = randPiece();
    curRot = 0;
    curX = 3;
    curY = -1;
    if (!canPlace(curPiece, curRot, curX, curY + 1))
        curY = 0; // allow start slightly visible
    if (!canPlace(curPiece, curRot, curX, curY))
        memset(board, 0, sizeof(board)); // simple game over: reset board
}

static void drawBlock(int x, int y, u32 color)
{
    int sx = originX + x * blockSize;
    int sy = originY + y * blockSize;
    int ex = sx + blockSize - 2;
    int ey = sy + blockSize - 2;
    gsKit_prim_sprite(gsGlobal, sx, sy, ex, ey, 2, color);
}

static void drawBoard(void)
{
    rmStartFrame();
    gsKit_clear(gsGlobal, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80));

    // Grid
    for (int y = 0; y < TETRIS_ROWS; y++) {
        for (int x = 0; x < TETRIS_COLS; x++) {
            u32 col = pieceColors[board[y][x]];
            drawBlock(x, y, col);
        }
    }

    // Active piece
    for (int i = 0; i < 4; i++) {
        int px = curX + pieces[curPiece][curRot][i][0];
        int py = curY + pieces[curPiece][curRot][i][1];
        if (py >= 0)
            drawBlock(px, py, pieceColors[curPiece + 1]);
    }

    // Next preview
    gsKit_prim_sprite(gsGlobal, originX + (TETRIS_COLS + 2) * blockSize, originY + blockSize,
                      originX + (TETRIS_COLS + 8) * blockSize, originY + 7 * blockSize, 2, GS_SETREG_RGBA(0x20, 0x20, 0x20, 0x80));
    for (int i = 0; i < 4; i++) {
        int px = TETRIS_COLS + 4 + pieces[nextPiece][0][i][0];
        int py = 2 + pieces[nextPiece][0][i][1];
        drawBlock(px, py, pieceColors[nextPiece + 1]);
    }

    rmEndFrame();
}

static void hardDrop(void)
{
    while (canPlace(curPiece, curRot, curX, curY + 1))
        curY++;
    placePiece();
    clearLines();
    spawnPiece();
    dropCounter = 0;
}

static void softDrop(void)
{
    if (canPlace(curPiece, curRot, curX, curY + 1)) {
        curY++;
    } else {
        placePiece();
        clearLines();
        spawnPiece();
    }
    dropCounter = 0;
}

static void tetrisInitLayout(void)
{
    int w, h;
    rmGetScreenExtentsNative(&w, &h);
    int maxW = (int)(w * 0.75f);
    int maxH = (int)(h * 0.85f);
    blockSize = maxW / TETRIS_COLS;
    int bh = maxH / TETRIS_ROWS;
    if (bh < blockSize)
        blockSize = bh;
    if (blockSize < 12)
        blockSize = 12;
    if (blockSize > 28)
        blockSize = 28;

    originX = (w - (blockSize * TETRIS_COLS)) / 2;
    originY = (h - (blockSize * TETRIS_ROWS)) / 2;
}

static void tetrisSplash(void)
{
    int w, h;
    rmGetScreenExtentsNative(&w, &h);
    int shown = 0;
    while (!shown) {
        rmStartFrame();
        gsKit_clear(gsGlobal, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80));
        fntRenderString(gTheme->fonts[0], w / 2, h / 2 - 20, ALIGN_CENTER, 0, 0, "TETRIS", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(gTheme->fonts[0], w / 2, h / 2 + 10, ALIGN_CENTER, 0, 0, "Created by Chase Bo Camp", GS_SETREG_RGBA(0xCC, 0xCC, 0xCC, 0x80));
        fntRenderString(gTheme->fonts[0], w / 2, h / 2 + 40, ALIGN_CENTER, 0, 0, "Press Start to begin", GS_SETREG_RGBA(0xAA, 0xAA, 0xAA, 0x80));
        rmEndFrame();
        readPads();
        if (getKeyOn(KEY_START) || getKeyOn(KEY_CROSS) || getKeyOn(KEY_TRIANGLE) || getKeyOn(KEY_SQUARE))
            shown = 1;
        usleep(16000);
    }
}

int tetrisSecretHandler(void)
{
    if (getKey(KEY_L1) && getKey(KEY_R1) && getKeyOn(KEY_START))
        return UIID_BTN_SECRET;
    return 0;
}

void tetrisRun(void)
{
    tetrisInitLayout();
    tetrisSplash();
    srand((unsigned)time(NULL));
    memset(board, 0, sizeof(board));
    nextPiece = randPiece();
    spawnPiece();
    dropCounter = 0;

    while (1) {
        readPads();

        if (getKeyOn(KEY_START))
            break;

        if (getKeyOn(KEY_LEFT) && canPlace(curPiece, curRot, curX - 1, curY))
            curX--;
        else if (getKeyOn(KEY_RIGHT) && canPlace(curPiece, curRot, curX + 1, curY))
            curX++;

        if (getKeyOn(KEY_CROSS)) {
            int newRot = (curRot + 1) & 3;
            if (canPlace(curPiece, newRot, curX, curY))
                curRot = newRot;
        }

        if (getKeyOn(KEY_DOWN))
            softDrop();

        if (getKeyOn(KEY_TRIANGLE))
            hardDrop();

        dropCounter++;
        if (dropCounter > 30) { // gravity
            softDrop();
        }

        drawBoard();
        usleep(16000); // ~60fps
    }
}
