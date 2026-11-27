#include "layout.h"

#if LAYOUT_SCALE_NUM * 5 == LAYOUT_SCALE_DEN * 8
// Design size x1.6, rounded to the nearest size u8g2 ships. Above 24 px the
// Helvetica / New Century / Courier families stop, so the closest-looking
// large families take over: Free Universal (sans), Old Standard (serif) and
// Inconsolata (mono).
static const uint8_t *const kMap[][2] = {
    {u8g2_font_helvR08_tf, u8g2_font_helvR12_tf}, {u8g2_font_helvB08_tf, u8g2_font_helvB12_tf},
    {u8g2_font_helvR10_tf, u8g2_font_helvR14_tf}, {u8g2_font_helvB10_tf, u8g2_font_helvB14_tf},
    {u8g2_font_helvR12_tf, u8g2_font_helvR18_tf}, {u8g2_font_helvB12_tf, u8g2_font_helvB18_tf},
    {u8g2_font_helvR14_tf, u8g2_font_helvR24_tf}, {u8g2_font_helvB14_tf, u8g2_font_helvB24_tf},
    {u8g2_font_helvR18_tf, u8g2_font_fur30_tf},   {u8g2_font_helvB18_tf, u8g2_font_fub30_tf},
    {u8g2_font_helvR24_tf, u8g2_font_fur35_tf},   {u8g2_font_helvB24_tf, u8g2_font_fub35_tf},

    {u8g2_font_ncenR08_tf, u8g2_font_ncenR12_tf}, {u8g2_font_ncenB08_tf, u8g2_font_ncenB12_tf},
    {u8g2_font_ncenR10_tf, u8g2_font_ncenR14_tf}, {u8g2_font_ncenB10_tf, u8g2_font_ncenB14_tf},
    {u8g2_font_ncenR12_tf, u8g2_font_ncenR18_tf}, {u8g2_font_ncenB12_tf, u8g2_font_ncenB18_tf},
    {u8g2_font_ncenR14_tf, u8g2_font_ncenR24_tf}, {u8g2_font_ncenB14_tf, u8g2_font_ncenB24_tf},
    {u8g2_font_ncenR18_tf, u8g2_font_osr29_tf},   {u8g2_font_ncenB18_tf, u8g2_font_osb29_tf},
    {u8g2_font_ncenR24_tf, u8g2_font_osr35_tf},   {u8g2_font_ncenB24_tf, u8g2_font_osb35_tf},

    {u8g2_font_courR08_tf, u8g2_font_courR12_tf}, {u8g2_font_courB08_tf, u8g2_font_courB12_tf},
    {u8g2_font_courR10_tf, u8g2_font_courR14_tf}, {u8g2_font_courB10_tf, u8g2_font_courB14_tf},
    {u8g2_font_courR12_tf, u8g2_font_courR18_tf}, {u8g2_font_courB12_tf, u8g2_font_courB18_tf},
    {u8g2_font_courR14_tf, u8g2_font_courR24_tf}, {u8g2_font_courB14_tf, u8g2_font_courB24_tf},
    {u8g2_font_courR18_tf, u8g2_font_inr30_mf},   {u8g2_font_courB18_tf, u8g2_font_inb30_mf},
    {u8g2_font_courR24_tf, u8g2_font_inr38_mf},   {u8g2_font_courB24_tf, u8g2_font_inb38_mf},
};

const uint8_t *SF(const uint8_t *f) {
  for (auto &m : kMap)
    if (m[0] == f) return m[1];
  return f;
}
#else
const uint8_t *SF(const uint8_t *f) { return f; }
#endif
