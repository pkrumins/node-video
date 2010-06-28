#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/*
found the algo how to convert rgb to yuv 420 here:

http://www.tech-archive.net/Archive/Development/microsoft.public.win32.programmer.directx.video/2007-09/msg00087.html
*/

#define rgbtoy(b, g, r, y) \
    y=(unsigned char)(((int)(30*r) + (int)(59*g) + (int)(11*b))/100)

#define rgbtoyuv(b, g, r, y, u, v) \
    rgbtoy(b, g, r, y); \
    u=(unsigned char)(((int)(-17*r) - (int)(33*g) + (int)(50*b)+12800)/100); \
    v=(unsigned char)(((int)(50*r) - (int)(42*g) - (int)(8*b)+12800)/100)

static void
rgba_to_yuv420 (
    const unsigned char * rgba,
    unsigned char * yuv,
    int srcFrameWidth,
    int srcFrameHeight
) {
    unsigned int planeSize;
    unsigned int halfWidth;

    unsigned char * yplane;
    unsigned char * uplane;
    unsigned char * vplane;
    const unsigned char * rgbaIndex;

    int x, y;
    unsigned char * yline;
    unsigned char * uline;
    unsigned char * vline;

    planeSize = srcFrameWidth * srcFrameHeight;
    halfWidth = srcFrameWidth >> 1;

    yplane = yuv;
    uplane = yuv + planeSize;
    vplane = yuv + planeSize + (planeSize >> 2);
    rgbaIndex = rgba;

    for (y = 0; y < srcFrameHeight; y++) {
        yline = yplane + (y * srcFrameWidth);
        uline = uplane + ((y >> 1) * halfWidth);
        vline = vplane + ((y >> 1) * halfWidth);

        for (x = 0; x < (int) srcFrameWidth; x+=2)
        {
            rgbtoyuv(rgbaIndex[0], rgbaIndex[1], rgbaIndex[2], *yline, *uline, *vline);
            rgbaIndex += 4;
            yline++;
            rgbtoyuv(rgbaIndex[0], rgbaIndex[1], rgbaIndex[2], *yline, *uline, *vline);
            rgbaIndex += 4;
            yline++;
            uline++;
            vline++;
        }
    }
}

static unsigned char *
rgba_to_yuv(unsigned char *rgba, int rgba_size)
{
    int yuv_size = rgba_size/4*3;
    unsigned char *yuv = (unsigned char *)malloc(sizeof(unsigned char)*yuv_size);
    if (!yuv) return NULL;
    int i, j;
    unsigned char r, g, b;
    unsigned int y, u, v;
    for (i=0,j=0;i<rgba_size;i+=4,j+=3) {
        r = rgba[i];
        g = rgba[i+1];
        b = rgba[i+2];
        
        y = ((19595*r + 38469*g + 7471*b) >> 16) & 0xFF;
        u = ((-11055*r - 21712*g + 32768*b + 8388608) >> 16) & 0xFF;
        v = ((32768*r - 27439*g - 5328*b + 8388608) >> 16) & 0xFF;
        
        yuv[j] = y;
        yuv[j+1] = u;
        yuv[j+2] = v;
    }
    return yuv;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: rgba2yuv <input file> <output file> <width> <height>\n");
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
    size_t yuv_size = width*height*3/2;

    unsigned char *rgba_buf = malloc(sizeof(unsigned char)*rgba_size);
    if (!rgba_buf) {
        printf("Could not malloc mem for rgba_buf\n");
        exit(1);
    }

    unsigned char *yuv_buf = malloc(sizeof(unsigned char)*yuv_size);
    if (!yuv_buf) {
        printf("Could not malloc yuv_buf\n");
        exit(1);
    }

    size_t n = fread(rgba_buf, sizeof(unsigned char), rgba_size, input);
    if (n != rgba_size) {
        printf("Didn't read %d bytes (width*height*4)\n", rgba_size);
        exit(1);
    }

    rgba_to_yuv420(rgba_buf, yuv_buf, width, height);

    if (fwrite(yuv_buf, sizeof(unsigned char), yuv_size, output) != yuv_size) {
        printf("Failed writing file output file %s, %s\n", outputf,
            strerror(errno));
        exit(1);
    }

    fclose(input);
    fclose(output);

    return 0;
}

