/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, sin-ack <sin-ack@protonmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/CharacterTypes.h>
#include <AK/Forward.h>
#include <AK/Utf32View.h>
#include <AK/Utf8View.h>
#include <AK/Variant.h>
#include <AK/Vector.h>
#include <LibGfx/Font/Font.h>
#include <LibGfx/FontCascadeList.h>
#include <LibGfx/Forward.h>
#include <LibGfx/Rect.h>
#include <LibGfx/TextElision.h>
#include <LibGfx/TextWrapping.h>

namespace Gfx {

// FIXME: This currently isn't an ideal way of doing things; ideally, TextLayout
// would be doing the rendering by painting individual glyphs. However, this
// would regress our Unicode bidirectional text support. Therefore, fixing this
// requires:
// - Moving the bidirectional algorithm either here, or some place TextLayout
//   can access;
// - Making TextLayout render the given text into something like a Vector<Line>
//   where:
//   using Line = Vector<DirectionalRun>;
//   struct DirectionalRun {
//       Utf32View glyphs;
//       Vector<int> advance;
//       TextDirection direction;
//   };
// - Either;
//   a) Making TextLayout output these Lines directly using a given Painter, or
//   b) Taking the Lines from TextLayout and painting each glyph.
class TextLayout {
public:
    TextLayout(Gfx::Font const& font, Utf8View const& text, FloatRect const& rect)
        : m_font(font)
        , m_font_metrics(font.pixel_metrics())
        , m_text(text)
        , m_rect(rect)
    {
    }

    Vector<ByteString, 32> lines(TextElision elision, TextWrapping wrapping) const
    {
        return wrap_lines(elision, wrapping);
    }

    FloatRect bounding_rect(TextWrapping) const;

private:
    Vector<ByteString, 32> wrap_lines(TextElision, TextWrapping) const;
    ByteString elide_text_from_right(Utf8View) const;

    Font const& m_font;
    FontPixelMetrics m_font_metrics;
    Utf8View m_text;
    FloatRect m_rect;
};

inline bool should_paint_as_space(u32 code_point)
{
    return is_ascii_space(code_point) || code_point == 0xa0;
}

enum class IncludeLeftBearing {
    Yes,
    No
};

struct DrawGlyph {
    FloatPoint position;
    u32 code_point;

    void translate_by(FloatPoint const& delta)
    {
        position.translate_by(delta);
    }
};

struct DrawEmoji {
    FloatPoint position;
    Gfx::Bitmap const* emoji;

    void translate_by(FloatPoint const& delta)
    {
        position.translate_by(delta);
    }
};

using DrawGlyphOrEmoji = Variant<DrawGlyph, DrawEmoji>;

class GlyphRun : public RefCounted<GlyphRun> {
public:
    enum class TextType {
        Common,
        ContextDependent,
        EndPadding,
        Ltr,
        Rtl,
    };

    GlyphRun(Vector<Gfx::DrawGlyphOrEmoji>&& glyphs, NonnullRefPtr<Font> font, TextType text_type)
        : m_glyphs(move(glyphs))
        , m_font(move(font))
        , m_text_type(text_type)
    {
    }

    [[nodiscard]] Font const& font() const { return m_font; }
    [[nodiscard]] TextType text_type() const { return m_text_type; }
    [[nodiscard]] Vector<Gfx::DrawGlyphOrEmoji> const& glyphs() const { return m_glyphs; }
    [[nodiscard]] Vector<Gfx::DrawGlyphOrEmoji>& glyphs() { return m_glyphs; }
    [[nodiscard]] bool is_empty() const { return m_glyphs.is_empty(); }

    void append(Gfx::DrawGlyphOrEmoji glyph) { m_glyphs.append(glyph); }

private:
    Vector<Gfx::DrawGlyphOrEmoji> m_glyphs;
    NonnullRefPtr<Font> m_font;
    TextType m_text_type;
};

Variant<DrawGlyph, DrawEmoji> prepare_draw_glyph_or_emoji(FloatPoint point, Utf8CodePointIterator& it, Font const& font);

template<typename Callback>
void for_each_glyph_position(FloatPoint baseline_start, Utf8View string, Gfx::Font const& font, Callback callback, IncludeLeftBearing include_left_bearing = IncludeLeftBearing::No, Optional<float&> width = {})
{
    float space_width = font.glyph_width(' ') + font.glyph_spacing();

    u32 last_code_point = 0;

    auto point = baseline_start;
    for (auto code_point_iterator = string.begin(); code_point_iterator != string.end(); ++code_point_iterator) {
        auto it = code_point_iterator; // The callback function will advance the iterator, so create a copy for this lookup.
        auto code_point = *code_point_iterator;

        point.set_y(baseline_start.y() - font.pixel_metrics().ascent);

        if (should_paint_as_space(code_point)) {
            point.translate_by(space_width, 0);
            last_code_point = code_point;
            continue;
        }

        auto kerning = font.glyphs_horizontal_kerning(last_code_point, code_point);
        if (kerning != 0.0f)
            point.translate_by(kerning, 0);

        auto glyph_width = font.glyph_or_emoji_width(it) + font.glyph_spacing();
        auto glyph_or_emoji = prepare_draw_glyph_or_emoji(point, code_point_iterator, font);
        if (include_left_bearing == IncludeLeftBearing::Yes) {
            if (glyph_or_emoji.has<DrawGlyph>())
                glyph_or_emoji.get<DrawGlyph>().position += FloatPoint(font.glyph_left_bearing(code_point), 0);
        }

        callback(glyph_or_emoji);

        point.translate_by(glyph_width, 0);
        last_code_point = code_point;
    }

    if (width.has_value())
        *width = point.x() - font.glyph_spacing();
}

}
