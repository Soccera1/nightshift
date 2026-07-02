#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ZipEntry {
    const char *name;
    uint32_t crc;
    uint32_t size;
    uint32_t local_offset;
} ZipEntry;

static uint32_t crc_table[256];

static void init_crc_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
        }
        crc_table[i] = crc;
    }
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc_table[(crc ^ data[i]) & 0xffu];
    }
    return ~crc;
}

static void put_u16(FILE *file, uint16_t value)
{
    fputc(value & 0xff, file);
    fputc((value >> 8) & 0xff, file);
}

static void put_u32(FILE *file, uint32_t value)
{
    put_u16(file, (uint16_t)(value & 0xffffu));
    put_u16(file, (uint16_t)(value >> 16));
}

static long file_size(FILE *file)
{
    long current = ftell(file);
    if (current < 0 || fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, current, SEEK_SET) != 0) {
        return -1;
    }
    return size;
}

static char *join_path(const char *root, const char *name)
{
    size_t root_len = strlen(root);
    size_t name_len = strlen(name);
    int needs_slash = root_len > 0 && root[root_len - 1] != '/';
    char *path = malloc(root_len + (size_t)needs_slash + name_len + 1);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, root, root_len);
    if (needs_slash) {
        path[root_len] = '/';
    }
    memcpy(path + root_len + (size_t)needs_slash, name, name_len + 1);
    return path;
}

static int read_file(const char *path, unsigned char **data, uint32_t *size, uint32_t *crc)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "make_zip: could not open %s: %s\n", path, strerror(errno));
        return 0;
    }

    long len = file_size(file);
    if (len < 0 || len > UINT32_MAX) {
        fprintf(stderr, "make_zip: unsupported file size for %s\n", path);
        fclose(file);
        return 0;
    }

    unsigned char *buffer = NULL;
    if (len > 0) {
        buffer = malloc((size_t)len);
        if (buffer == NULL) {
            fclose(file);
            return 0;
        }
        if (fread(buffer, 1, (size_t)len, file) != (size_t)len) {
            fprintf(stderr, "make_zip: could not read %s\n", path);
            free(buffer);
            fclose(file);
            return 0;
        }
    }
    fclose(file);

    *data = buffer;
    *size = (uint32_t)len;
    *crc = crc32_update(0, buffer, (size_t)len);
    return 1;
}

static int write_local_entry(FILE *zip, const char *name, const unsigned char *data, uint32_t size, uint32_t crc)
{
    size_t name_len = strlen(name);
    if (name_len > UINT16_MAX) {
        fprintf(stderr, "make_zip: path too long: %s\n", name);
        return 0;
    }

    put_u32(zip, 0x04034b50u);
    put_u16(zip, 20);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u32(zip, crc);
    put_u32(zip, size);
    put_u32(zip, size);
    put_u16(zip, (uint16_t)name_len);
    put_u16(zip, 0);
    fwrite(name, 1, name_len, zip);
    if (size > 0) {
        fwrite(data, 1, size, zip);
    }
    return ferror(zip) == 0;
}

static int write_central_entry(FILE *zip, const ZipEntry *entry)
{
    size_t name_len = strlen(entry->name);
    if (name_len > UINT16_MAX) {
        return 0;
    }

    put_u32(zip, 0x02014b50u);
    put_u16(zip, 20);
    put_u16(zip, 20);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u32(zip, entry->crc);
    put_u32(zip, entry->size);
    put_u32(zip, entry->size);
    put_u16(zip, (uint16_t)name_len);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u32(zip, 0);
    put_u32(zip, entry->local_offset);
    fwrite(entry->name, 1, name_len, zip);
    return ferror(zip) == 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s OUT.zip ROOT FILE...\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *output_path = argv[1];
    const char *root = argv[2];
    int entry_count = argc - 3;
    ZipEntry *entries = calloc((size_t)entry_count, sizeof(*entries));
    if (entries == NULL) {
        return EXIT_FAILURE;
    }

    init_crc_table();

    FILE *zip = fopen(output_path, "wb");
    if (zip == NULL) {
        fprintf(stderr, "make_zip: could not create %s: %s\n", output_path, strerror(errno));
        free(entries);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < entry_count; i++) {
        const char *name = argv[i + 3];
        char *path = join_path(root, name);
        unsigned char *data = NULL;
        uint32_t size = 0;
        uint32_t crc = 0;
        if (path == NULL || !read_file(path, &data, &size, &crc)) {
            free(path);
            fclose(zip);
            free(entries);
            return EXIT_FAILURE;
        }

        long offset = ftell(zip);
        if (offset < 0 || offset > UINT32_MAX) {
            fprintf(stderr, "make_zip: archive too large\n");
            free(data);
            free(path);
            fclose(zip);
            free(entries);
            return EXIT_FAILURE;
        }

        entries[i] = (ZipEntry){
            .name = name,
            .crc = crc,
            .size = size,
            .local_offset = (uint32_t)offset,
        };
        if (!write_local_entry(zip, name, data, size, crc)) {
            fprintf(stderr, "make_zip: could not write %s\n", output_path);
            free(data);
            free(path);
            fclose(zip);
            free(entries);
            return EXIT_FAILURE;
        }
        free(data);
        free(path);
    }

    long central_offset = ftell(zip);
    if (central_offset < 0 || central_offset > UINT32_MAX) {
        fprintf(stderr, "make_zip: archive too large\n");
        fclose(zip);
        free(entries);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < entry_count; i++) {
        if (!write_central_entry(zip, &entries[i])) {
            fprintf(stderr, "make_zip: could not write central directory\n");
            fclose(zip);
            free(entries);
            return EXIT_FAILURE;
        }
    }

    long end_offset = ftell(zip);
    if (end_offset < central_offset || end_offset - central_offset > UINT32_MAX || entry_count > UINT16_MAX) {
        fprintf(stderr, "make_zip: archive too large\n");
        fclose(zip);
        free(entries);
        return EXIT_FAILURE;
    }

    put_u32(zip, 0x06054b50u);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, (uint16_t)entry_count);
    put_u16(zip, (uint16_t)entry_count);
    put_u32(zip, (uint32_t)(end_offset - central_offset));
    put_u32(zip, (uint32_t)central_offset);
    put_u16(zip, 0);

    int ok = fclose(zip) == 0;
    free(entries);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
