
#pragma once

class CmtGlyphRenderer
{
public:
    CmtGlyphRenderer(float glyph_height, float vline_width);

    void drawPrevious();
    void drawBackward();
    void drawForward();
    void drawNext();
    void drawStop();
    void drawRecord(bool active);
    void drawPlay(bool active);
    void drawPause(bool active);
    void drawEject();

private:
    float GLYPH_HEIGHT;      // 30.0f
    float GLYPH_VLINE_WIDTH; // 8.0f
    float GLYPH_TRIANGLE_WIDTH;
    float GLYPH_TRIANGLE_HEIGHT;
    float GLYPH_STOP_WIDTH;
    float GLYPH_STOP_HEIGHT;
    float GLYPH_RECORD_RADIUS;
};
