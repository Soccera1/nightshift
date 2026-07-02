#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    ICON_W = 32,
    ICON_H = 32,
    XOR_BYTES = ICON_W * ICON_H * 4,
    AND_STRIDE = 4,
    AND_BYTES = AND_STRIDE * ICON_H,
    BMP_HEADER_BYTES = 40,
    IMAGE_BYTES = BMP_HEADER_BYTES + XOR_BYTES + AND_BYTES,
    ICO_OFFSET = 6 + 16,
};

typedef struct Pixel {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} Pixel;

static void put_u16(FILE *file, uint16_t value)
{
    fputc((int)(value & 0xffu), file);
    fputc((int)((value >> 8) & 0xffu), file);
}

static void put_u32(FILE *file, uint32_t value)
{
    fputc((int)(value & 0xffu), file);
    fputc((int)((value >> 8) & 0xffu), file);
    fputc((int)((value >> 16) & 0xffu), file);
    fputc((int)((value >> 24) & 0xffu), file);
}

static void fill_rect(Pixel pixels[ICON_H][ICON_W], int x, int y, int w, int h, Pixel color)
{
    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= ICON_H) {
            continue;
        }
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= ICON_W) {
                continue;
            }
            pixels[py][px] = color;
        }
    }
}

static Pixel rgba(uint8_t r, uint8_t g, uint8_t b)
{
    return (Pixel){ .b = b, .g = g, .r = r, .a = 255 };
}

static void draw_icon(Pixel pixels[ICON_H][ICON_W])
{
    fill_rect(pixels, 0, 0, ICON_W, ICON_H, rgba(7, 9, 10));
    fill_rect(pixels, 3, 4, 26, 24, rgba(24, 29, 30));
    fill_rect(pixels, 5, 6, 22, 20, rgba(52, 58, 58));
    fill_rect(pixels, 8, 9, 6, 6, rgba(217, 70, 55));
    fill_rect(pixels, 18, 9, 6, 6, rgba(217, 70, 55));
    fill_rect(pixels, 9, 20, 14, 3, rgba(219, 226, 211));
    fill_rect(pixels, 11, 25, 10, 2, rgba(211, 175, 92));
    fill_rect(pixels, 5, 2, 3, 4, rgba(115, 128, 119));
    fill_rect(pixels, 24, 2, 3, 4, rgba(115, 128, 119));
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.ico\n", argv[0]);
        return EXIT_FAILURE;
    }

    Pixel pixels[ICON_H][ICON_W] = { 0 };
    draw_icon(pixels);

    FILE *file = fopen(argv[1], "wb");
    if (file == NULL) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    put_u16(file, 0);
    put_u16(file, 1);
    put_u16(file, 1);
    fputc(ICON_W, file);
    fputc(ICON_H, file);
    fputc(0, file);
    fputc(0, file);
    put_u16(file, 1);
    put_u16(file, 32);
    put_u32(file, IMAGE_BYTES);
    put_u32(file, ICO_OFFSET);

    put_u32(file, BMP_HEADER_BYTES);
    put_u32(file, ICON_W);
    put_u32(file, ICON_H * 2);
    put_u16(file, 1);
    put_u16(file, 32);
    put_u32(file, 0);
    put_u32(file, XOR_BYTES + AND_BYTES);
    put_u32(file, 0);
    put_u32(file, 0);
    put_u32(file, 0);
    put_u32(file, 0);

    for (int y = ICON_H - 1; y >= 0; y--) {
        for (int x = 0; x < ICON_W; x++) {
            Pixel pixel = pixels[y][x];
            fwrite(&pixel, sizeof(pixel), 1, file);
        }
    }
    for (int y = 0; y < ICON_H; y++) {
        for (int x = 0; x < AND_STRIDE; x++) {
            fputc(0, file);
        }
    }

    if (fclose(file) != 0) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
