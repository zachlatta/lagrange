#include "text.h"
#include "color.h"
#include "metrics.h"
#include "embedded.h"
#include "app.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"

#include <the_Foundation/array.h>
#include <the_Foundation/file.h>
#include <the_Foundation/hash.h>
#include <the_Foundation/math.h>
#include <the_Foundation/path.h>
#include <the_Foundation/vec2.h>

#include <SDL_surface.h>
#include <stdarg.h>

iDeclareType(Glyph)
iDeclareTypeConstructionArgs(Glyph, iChar ch)

struct Impl_Glyph {
    iHashNode node;
    iRect rect;
    int advance;
    int dx, dy;
};

void init_Glyph(iGlyph *d, iChar ch) {
    d->node.key = ch;
    d->rect = zero_Rect();
    d->advance = 0;
}

void deinit_Glyph(iGlyph *d) {
    iUnused(d);
}

iChar char_Glyph(const iGlyph *d) {
    return d->node.key;
}

iDefineTypeConstructionArgs(Glyph, (iChar ch), ch)

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(Font)

struct Impl_Font {
    iBlock *       data;
    stbtt_fontinfo font;
    float          scale;
    int            height;
    int            baseline;
    iHash          glyphs;
};

static void init_Font(iFont *d, const iBlock *data, int height) {
    init_Hash(&d->glyphs);
    d->data = NULL;
    d->height = height;
    iZap(d->font);
    stbtt_InitFont(&d->font, constData_Block(data), 0);
    d->scale = stbtt_ScaleForPixelHeight(&d->font, height);
    int ascent;
    stbtt_GetFontVMetrics(&d->font, &ascent, 0, 0);
    d->baseline = (int) ascent * d->scale;
}

static void deinit_Font(iFont *d) {
    iForEach(Hash, i, &d->glyphs) {
        delete_Glyph((iGlyph *) i.value);
    }
    deinit_Hash(&d->glyphs);
    delete_Block(d->data);
}

iDeclareType(Text)

struct Impl_Text {
    iFont         fonts[max_FontId];
    SDL_Renderer *render;
    SDL_Texture * cache;
    iInt2         cacheSize;
    iInt2         cachePos;
    int           cacheRowHeight;
    SDL_Palette * grayscale;
};

static iText text_;

void init_Text(SDL_Renderer *render) {
    iText *d = &text_;
    d->render = render;
    /* A grayscale palette for rasterized glyphs. */ {
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i) {
            colors[i] = (SDL_Color){ 255, 255, 255, i };
        }
        d->grayscale = SDL_AllocPalette(256);
        SDL_SetPaletteColors(d->grayscale, colors, 0, 256);
    }
    /* Initialize the glyph cache. */ {
        d->cacheSize = init1_I2(fontSize_UI * 16);
        d->cachePos  = zero_I2();
        d->cache     = SDL_CreateTexture(render,
                                     SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                     d->cacheSize.x,
                                     d->cacheSize.y);
        SDL_SetTextureBlendMode(d->cache, SDL_BLENDMODE_BLEND);
        d->cacheRowHeight = 0;
    }
    /* Load the fonts. */ {
        const struct { const iBlock *ttf; int size; } fontData[max_FontId] = {
            { &fontFiraSansRegular_Embedded, fontSize_UI },
            { &fontFiraMonoRegular_Embedded, fontSize_UI },
            { &fontFiraSansRegular_Embedded, fontSize_UI * 1.5f },
            { &fontFiraSansLightItalic_Embedded, fontSize_UI },
            { &fontFiraSansBold_Embedded, fontSize_UI },
            { &fontFiraSansBold_Embedded, fontSize_UI * 1.5f },
            { &fontFiraSansBold_Embedded, fontSize_UI * 1.75f },
            { &fontFiraSansBold_Embedded, fontSize_UI * 2.0f },
        };
        iForIndices(i, fontData) {
            init_Font(&d->fonts[i], fontData[i].ttf, fontData[i].size);
        }
    }
}

void deinit_Text(void) {
    iText *d = &text_;
    SDL_FreePalette(d->grayscale);
    iForIndices(i, d->fonts) {
        deinit_Font(&d->fonts[i]);
    }
    SDL_DestroyTexture(d->cache);
    d->render = NULL;
}

static SDL_Surface *rasterizeGlyph_Font_(const iFont *d, iChar ch) {
    int w, h;
    uint8_t *bmp = stbtt_GetCodepointBitmap(&d->font, d->scale, d->scale, ch, &w, &h, 0, 0);
    /* Note: `bmp` must be freed afterwards. */
    SDL_Surface *surface =
        SDL_CreateRGBSurfaceWithFormatFrom(bmp, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
    SDL_SetSurfacePalette(surface, text_.grayscale);
    return surface;
}

static iBool isSpecialChar_(iChar ch) {
    return ch >= specialSymbol_Text && ch < 0x20;
}

static float symbolEmWidth_(int symbol) {
    return 1.5f;
}

static float symbolAdvance_(int symbol) {
    return 1.5f;
}

static int specialChar_(iChar ch) {
    return ch - specialSymbol_Text;
}

iLocalDef SDL_Rect sdlRect_(const iRect rect) {
    return (SDL_Rect){ rect.pos.x, rect.pos.y, rect.size.x, rect.size.y };
}

#if 0
static void fillTriangle_(SDL_Surface *surface, const SDL_Rect *rect, int dir) {
    const uint32_t color = 0xffffffff;
    SDL_LockSurface(surface);
    uint32_t *row = surface->pixels;
    row += rect->x + rect->y * surface->pitch / 4;
    for (int y = 0; y < rect->h; y++, row += surface->pitch / 4) {
        float norm = (float) y / (float) (rect->h - 1) * 2.0f;
        if (norm > 1.0f) norm = 2.0f - norm;
        const int      len        = norm * rect->w;
        const float    fract      = norm * rect->w - len;
        const uint32_t fractColor = 0xffffff00 | (int) (fract * 0xff);
        if (dir > 0) {
            for (int x = 0; x < len; x++) {
                row[x] = color;
            }
            if (len < rect->w) {
                row[len] = fractColor;
            }
        }
        else {
            for (int x = 0; x < len; x++) {
                row[rect->w - len + x] = color;
            }
            if (len < rect->w) {
                row[rect->w - len - 1] = fractColor;
            }
        }
    }
    SDL_UnlockSurface(surface);
}
#endif

static void cache_Font_(iFont *d, iGlyph *glyph) {
    iText *txt = &text_;
    SDL_Renderer *render = txt->render;
    SDL_Texture *tex = NULL;
    SDL_Surface *surface = NULL;
    const iChar ch = char_Glyph(glyph);
    iBool fromStb = iFalse;
    if (!isSpecialChar_(ch)) {
        /* Rasterize the glyph using stbtt. */
        surface = rasterizeGlyph_Font_(d, ch);
        stbtt_GetCodepointBitmapBox(
            &d->font, ch, d->scale, d->scale, &glyph->dx, &glyph->dy, NULL, NULL);
        fromStb = iTrue;
        tex = SDL_CreateTextureFromSurface(render, surface);
        glyph->rect.size = init_I2(surface->w, surface->h);
    }
    else {
        /* Metrics for special symbols. */
        int em, lsb;
        const int symbol = specialChar_(ch);
        stbtt_GetCodepointHMetrics(&d->font, 'M', &em, &lsb);
        glyph->dx = d->baseline / 10;
        glyph->dy = -d->baseline;
        glyph->rect.size = init_I2(symbolEmWidth_(symbol) * em * d->scale, d->height);
#if 0
        if (isRasterizedSymbol_(ch)) {
            /* Rasterize manually. */
            surface = SDL_CreateRGBSurfaceWithFormat(
                0, width_Rect(glyph->rect), height_Rect(glyph->rect), 32, SDL_PIXELFORMAT_RGBA8888);
            SDL_FillRect(surface, NULL, 0);
            const uint32_t white = 0xffffffff;
            switch (specialChar_(ch)) {
                case play_SpecialSymbol:
                    fillTriangle_(surface, &(SDL_Rect){ 0, 0, surface->w, d->baseline }, 1);
                    break;
                case pause_SpecialSymbol: {
                    const int w = surface->w * 4 / 11;
                    SDL_FillRect(surface, &(SDL_Rect){ 0, 0, w, d->baseline }, white);
                    SDL_FillRect(surface, &(SDL_Rect){ surface->w - w, 0, w, d->baseline }, white);
                    break;
                }
                case rewind_SpecialSymbol: {
                    const int w1 = surface->w / 7;
                    const int w2 = surface->w * 3 / 7;
                    const int h = d->baseline * 4 / 5;
                    const int off = (d->baseline - h) / 2;
                    SDL_FillRect(surface,  &(SDL_Rect){ 0,  off, w1, h}, white);
                    fillTriangle_(surface, &(SDL_Rect){ w1, off, w2, h }, -1);
                    fillTriangle_(surface, &(SDL_Rect){ surface->w * 4 / 7, off, w2, h }, -1);
                    break;
                }
            }
            tex = SDL_CreateTextureFromSurface(render, surface);
        }
#endif
    }
    /* Determine placement in the glyph cache texture, advancing in rows. */
    if (txt->cachePos.x + glyph->rect.size.x > txt->cacheSize.x) {
        txt->cachePos.x = 0;
        txt->cachePos.y += txt->cacheRowHeight;
        txt->cacheRowHeight = 0;
    }
    glyph->rect.pos = txt->cachePos;
    SDL_SetRenderTarget(render, txt->cache);
    const SDL_Rect dstRect = sdlRect_(glyph->rect);
    if (surface) {
        SDL_RenderCopy(render, tex, &(SDL_Rect){ 0, 0, dstRect.w, dstRect.h }, &dstRect);
    }
    else {
        /* Draw a special symbol. */
        SDL_SetRenderDrawColor(render, 255, 255, 255, 255);
        const iInt2 tl = init_I2(dstRect.x, dstRect.y);
        const iInt2 br = init_I2(dstRect.x + dstRect.w - 1, dstRect.y + dstRect.h - 1);
        const int midX = tl.x + dstRect.w / 2;
        const int midY = tl.y + dstRect.h / 2;
        const int symH = dstRect.h * 2 / 6;
#if 0
        /* Frame. */
        if (isFramedSymbol_(ch)) {
            SDL_RenderDrawLines(
                render,
                (SDL_Point[]){
                    { tl.x, tl.y }, { br.x, tl.y }, { br.x, br.y }, { tl.x, br.y }, { tl.x, tl.y } },
                5);
        }
        iArray points;
        init_Array(&points, sizeof(SDL_Point));
        switch (specialChar_(ch)) {
            case 0: /* silence */
                break;
            case 1: /* sine */
                for (int i = 0; i < dstRect.w; ++i) {
                    float rad = 2.0f * iMathPif * (float) i / dstRect.w;
                    SDL_Point pt = { tl.x + i, midY + sin(rad) * symH};
                    pushBack_Array(&points, &pt);
                }
                SDL_RenderDrawLines(render, constData_Array(&points), size_Array(&points));
                break;
            case 2: /* square */
                SDL_RenderDrawLines(render,
                                    (SDL_Point[]){ { tl.x, midY - symH },
                                                   { midX, midY - symH },
                                                   { midX, midY + symH },
                                                   { br.x, midY + symH } },
                                    4);
                break;
            case 3: /* saw */
                SDL_RenderDrawLines(render,
                                    (SDL_Point[]){ { tl.x, midY },
                                                   { midX, midY - symH },
                                                   { midX, midY + symH },
                                                   { br.x, midY } },
                                    4);
                break;
            case 4: /* triangle */
                SDL_RenderDrawLines(render,
                                    (SDL_Point[]){ { tl.x, midY },
                                                   { tl.x + dstRect.w / 4, midY - symH },
                                                   { br.x - dstRect.w / 4, midY + symH },
                                                   { br.x, midY } },
                                    4);
                break;
            case 5: /* noise */
                for (int i = 0; i < dstRect.w; ++i) {
                    for (int p = 0; p < 2; ++p) {
                        const float val = iRandomf() * 2.0f - 1.0f;
                        pushBack_Array(&points, &(SDL_Point){ tl.x + i, midY - val * symH });
                    }
                }
                SDL_RenderDrawPoints(render, constData_Array(&points), size_Array(&points));
                break;
        }        
        deinit_Array(&points);
#endif
    }
    SDL_SetRenderTarget(render, NULL);
    if (tex) {
        SDL_DestroyTexture(tex);
        iAssert(surface);
        if (fromStb) stbtt_FreeBitmap(surface->pixels, NULL);
        SDL_FreeSurface(surface);
    }
    /* Update cache cursor. */
    txt->cachePos.x += glyph->rect.size.x;
    txt->cacheRowHeight = iMax(txt->cacheRowHeight, glyph->rect.size.y);
}

static const iGlyph *glyph_Font_(iFont *d, iChar ch) {
    const void *node = value_Hash(&d->glyphs, ch);
    if (node) {
        return node;
    }
    iGlyph *glyph = new_Glyph(ch);
    cache_Font_(d, glyph);
    insert_Hash(&d->glyphs, &glyph->node);
    return glyph;
}

enum iRunMode { measure_RunMode, draw_RunMode, drawPermanentColor_RunMode };

static iInt2 run_Font_(iFont *d, enum iRunMode mode, const char *text, size_t maxLen, iInt2 pos,
                       int *runAdvance_out) {
    iInt2 size = zero_I2();
    const iInt2 orig = pos;
    const stbtt_fontinfo *info = &d->font;
    float xpos = pos.x;
    float xposMax = xpos;
    const iString textStr = iStringLiteral(text);
    iConstForEach(String, i, &textStr) {
        iChar ch = i.value;
        /* Special instructions. */ {
            if (ch == '\n') {
                xpos = pos.x;
                pos.y += d->height;
                continue;
            }
            if (ch == '\r') {
                next_StringConstIterator(&i);
                const iColor clr = get_Color(i.value - '0');
                if (mode == draw_RunMode) {
                    SDL_SetTextureColorMod(text_.cache, clr.r, clr.g, clr.b);
                }
                continue;
            }
        }
        const iGlyph *glyph = glyph_Font_(d, ch);
        int x1 = iRound(xpos);
        int x2 = x1 + glyph->rect.size.x;
        size.x = iMax(size.x, x2 - orig.x);
        size.y = iMax(size.y, pos.y + d->height - orig.y);
        if (mode != measure_RunMode) {
            SDL_Rect dst = { x1 + glyph->dx,
                             pos.y + d->baseline + glyph->dy,
                             glyph->rect.size.x,
                             glyph->rect.size.y };
            SDL_RenderCopy(text_.render, text_.cache, (const SDL_Rect *) &glyph->rect, &dst);
        }
        int advance, lsb;
        const iBool spec = isSpecialChar_(ch);
        stbtt_GetCodepointHMetrics(info, spec ? 'M' : ch, &advance, &lsb);
        if (spec) {
            advance *= symbolAdvance_(specialChar_(ch));
        }
        xpos += d->scale * advance;
        xposMax = iMax(xposMax, xpos);
        /* Check the next character. */ {
            iStringConstIterator j = i;
            next_StringConstIterator(&j);
            const iChar next = j.value;
            if (next) {
                xpos += d->scale * stbtt_GetCodepointKernAdvance(info, ch, next);
            }
        }
        if (--maxLen == 0) {
            break;
        }
    }
    if (runAdvance_out) {
        *runAdvance_out = xposMax - orig.x;
    }
    return size;
}

int lineHeight_Text(int fontId) {
    return text_.fonts[fontId].height;
}

iInt2 measure_Text(int fontId, const char *text) {
    if (!*text) {
        return init_I2(0, lineHeight_Text(fontId));
    }
    return run_Font_(&text_.fonts[fontId], measure_RunMode, text, iInvalidSize, zero_I2(), NULL);
}

iInt2 advance_Text(int fontId, const char *text) {
    int advance;
    const int height =
        run_Font_(&text_.fonts[fontId], measure_RunMode, text, iInvalidSize, zero_I2(), &advance).y;
    return init_I2(advance, height); //lineHeight_Text(fontId));
}

iInt2 advanceN_Text(int fontId, const char *text, size_t n) {
    if (n == 0) {
        return init_I2(0, lineHeight_Text(fontId));
    }
    int advance;
    run_Font_(&text_.fonts[fontId], measure_RunMode, text, n, zero_I2(), &advance);
    return init_I2(advance, lineHeight_Text(fontId));
}

iInt2 advanceRange_Text(int fontId, iRangecc text) {
    /* TODO: Rangecc should be the default for runs; no need to copy here */
    iString str;
    initRange_String(&str, text);
    const iInt2 metrics = advance_Text(fontId, cstr_String(&str));
    deinit_String(&str);
    return metrics;
}

static void draw_Text_(int fontId, iInt2 pos, int color, const char *text) {
    iText *d = &text_;
    const iColor clr = get_Color(color & mask_ColorId);
    SDL_SetTextureColorMod(d->cache, clr.r, clr.g, clr.b);
    run_Font_(&d->fonts[fontId],
              color & permanent_ColorId ? drawPermanentColor_RunMode : draw_RunMode,
              text,
              iInvalidSize,
              pos,
              NULL);
}

void draw_Text(int fontId, iInt2 pos, int color, const char *text, ...) {
    iBlock chars;
    init_Block(&chars, 0); {
        va_list args;
        va_start(args, text);
        vprintf_Block(&chars, text, args);
        va_end(args);
    }
    if (pos.x < 0) {
        /* Right-aligned. */
        pos.x = -pos.x - measure_Text(fontId, cstr_Block(&chars)).x;
    }
    if (pos.y < 0) {
        /* Bottom-aligned. */
        pos.y = -pos.y - lineHeight_Text(fontId);
    }
    draw_Text_(fontId, pos, color, cstr_Block(&chars));
    deinit_Block(&chars);
}

void drawString_Text(int fontId, iInt2 pos, int color, const iString *text) {
    draw_Text_(fontId, pos, color, cstr_String(text));
}

void drawCentered_Text(int fontId, iRect rect, int color, const char *text, ...) {
    iBlock chars;
    init_Block(&chars, 0); {
        va_list args;
        va_start(args, text);
        vprintf_Block(&chars, text, args);
        va_end(args);
    }
    const iInt2 textSize = advance_Text(fontId, cstr_Block(&chars));
    draw_Text_(fontId, sub_I2(mid_Rect(rect), divi_I2(textSize, 2)), color, cstr_Block(&chars));
    deinit_Block(&chars);
}

SDL_Texture *glyphCache_Text(void) {
    return text_.cache;
}

/*-----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(TextBuf, (int font, const char *text), font, text)

void init_TextBuf(iTextBuf *d, int font, const char *text) {
    SDL_Renderer *render = text_.render;
    d->size    = advance_Text(font, text);
    d->texture = SDL_CreateTexture(render,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                   d->size.x,
                                   d->size.y);
    SDL_SetTextureBlendMode(d->texture, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(render, d->texture);
    draw_Text_(font, zero_I2(), white_ColorId, text);
    SDL_SetRenderTarget(render, NULL);
}

void deinit_TextBuf(iTextBuf *d) {
    SDL_DestroyTexture(d->texture);
}

void draw_TextBuf(const iTextBuf *d, iInt2 pos, int color) {
    const iColor clr = get_Color(color);
    SDL_SetTextureColorMod(d->texture, clr.r, clr.g, clr.b);
    SDL_RenderCopy(text_.render,
                   d->texture,
                   &(SDL_Rect){ 0, 0, d->size.x, d->size.y },
                   &(SDL_Rect){ pos.x, pos.y, d->size.x, d->size.y });
}
