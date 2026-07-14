#ifndef UTF8_H
#define UTF8_H

/* Encode a Unicode codepoint into UTF-8 bytes in buf.
 * Returns number of bytes written (1-4), or 0 if invalid. */
static inline int utf8_encode(int codepoint, char *buf)
{
    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint < 0x110000) {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

/* Return the number of bytes in a UTF-8 sequence starting at byte c */
static inline int utf8_bytes(unsigned char c)
{
    if (c < 0x80) return 1;
    if (c < 0xC0) return 1;  /* continuation byte, treat as 1 */
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    return 4;
}

#endif /* UTF8_H */
