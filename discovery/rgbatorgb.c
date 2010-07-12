#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

void
rgba_to_rgb(unsigned char *rgba, unsigned char *rgb, int width, int height)
{
    int i, j;
    for (i=0,j=0;i<width*height*4;i+=4,j+=3) {
        rgb[j] = rgba[i];
        rgb[j+1] = rgba[i+1];
        rgb[j+2] = rgba[i+2];
    }
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: rgba2rgb <input file> <output file> <width> <height>\n");
        exit(1);
    }
    char *inputf = argv[1];
    char *outputf = argv[2];
    int width = atoi(argv[3]);
    int height = atoi(argv[4]);

    FILE *input = fopen(inputf, "r");
    if (!input) {
        printf("Could not open %s input file\n", inputf);
        exit(1);
    }
    FILE *output = fopen(outputf, "w+");
    if (!output) {
        printf("Could not open %s output file\n", outputf);
        exit(1);
    }

    size_t rgba_size = width*height*4;
    size_t rgb_size = width*height*3;

    unsigned char *rgba_buf = malloc(sizeof(unsigned char)*rgba_size);
    if (!rgba_buf) {
        printf("Could not malloc mem for rgba_buf\n");
        exit(1);
    }

    unsigned char *rgb_buf = malloc(sizeof(unsigned char)*rgb_size);
    if (!rgb_buf) {
        printf("Could not malloc rgb_buf\n");
        exit(1);
    }

    size_t n = fread(rgba_buf, sizeof(unsigned char), rgba_size, input);
    if (n != rgba_size) {
        printf("Didn't read %d bytes (width*height*4)\n", rgba_size);
        exit(1);
    }

    rgba_to_rgb(rgba_buf, rgb_buf, width, height);

    if (fwrite(rgb_buf, sizeof(unsigned char), rgb_size, output) != rgb_size) {
        printf("Failed writing file output file %s, %s\n", outputf,
            strerror(errno));
        exit(1);
    }

    free(rgba_buf);
    free(rgb_buf);
    fclose(input);
    fclose(output);

    return 0;
}

