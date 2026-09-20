#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlib.h>

#define PAYLOAD_SIZE (1024 * 1024)
#define ITERATIONS 100

static double elapsed_seconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static void build_payload(unsigned char* payload, size_t size)
{
    static const char row[] =
        "{\"id\":12345,\"username\":\"artem\",\"active\":true,"
        "\"message\":\"response compression benchmark payload\"}\n";

    size_t offset = 0;
    while (offset < size)
    {
        size_t remaining = size - offset;
        size_t chunk = sizeof(row) - 1;
        if (chunk > remaining)
            chunk = remaining;
        memcpy(payload + offset, row, chunk);
        offset += chunk;
    }
}

static int gzip_once(const unsigned char* input, size_t input_size, int level, unsigned char* output, size_t output_capacity, size_t* output_size)
{
    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    if (deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return 0;

    stream.next_in = (Bytef*)input;
    stream.avail_in = (uInt)input_size;
    stream.next_out = output;
    stream.avail_out = (uInt)output_capacity;

    int result = deflate(&stream, Z_FINISH);
    if (result != Z_STREAM_END)
    {
        deflateEnd(&stream);
        return 0;
    }

    *output_size = output_capacity - stream.avail_out;
    deflateEnd(&stream);
    return 1;
}

int main(void)
{
    unsigned char* payload = malloc(PAYLOAD_SIZE);
    if (!payload)
        return 1;
    build_payload(payload, PAYLOAD_SIZE);

    uLong bound = compressBound(PAYLOAD_SIZE) + 64;
    unsigned char* compressed = malloc((size_t)bound);
    if (!compressed)
    {
        free(payload);
        return 1;
    }

    const int levels[] = {1, 5, 9};
    puts("level,input_bytes,compressed_bytes,ratio,throughput_mib_s");

    for (size_t level_index = 0; level_index < sizeof(levels) / sizeof(levels[0]); level_index++)
    {
        int level = levels[level_index];
        size_t compressed_size = 0;

        struct timespec start;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int iteration = 0; iteration < ITERATIONS; iteration++)
        {
            if (!gzip_once(payload, PAYLOAD_SIZE, level, compressed, (size_t)bound, &compressed_size))
            {
                free(compressed);
                free(payload);
                return 1;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        double seconds = elapsed_seconds(start, end);
        double mib = ((double)PAYLOAD_SIZE * ITERATIONS) / (1024.0 * 1024.0);
        double ratio = (double)compressed_size / (double)PAYLOAD_SIZE;

        printf("%d,%d,%zu,%.4f,%.2f\n", level, PAYLOAD_SIZE, compressed_size, ratio, mib / seconds);
    }

    free(compressed);
    free(payload);
    return 0;
}
