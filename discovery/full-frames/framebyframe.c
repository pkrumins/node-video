#include <theora/theoraenc.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static int chroma_format = TH_PF_420;

static inline unsigned char
clamp(double d)
{
    if(d < 0) return 0;
    if(d > 255) return 255;
    return d;
}

static void
rgba_to_yuv(unsigned char *rgba, unsigned char *yuv, unsigned int w, unsigned int h)
{
    unsigned int rgba_size = w*h*4;
    unsigned char r, g, b;
    size_t i, j;

    for (i=0,j=0; i<rgba_size; i+=4, j+=3) {
        r = rgba[i];
        g = rgba[i+1];
        b = rgba[i+2];

        yuv[j] = clamp(0.299 * r + 0.587 * g + 0.114 * b);
        yuv[j+1] = clamp((0.436 * 255 - 0.14713 * r - 0.28886 * g + 0.436 * b) / 0.872);
        yuv[j+2] = clamp((0.615 * 255 + 0.615 * r - 0.51499 * g - 0.10001 * b) / 1.230);
    }
}

static int
theora_write_frame(int i, FILE *ogg_fp, ogg_stream_state *ogg_os, th_enc_ctx *td, int last)
{
  th_ycbcr_buffer ycbcr;
  ogg_packet op;
  ogg_page og;
  char terminal_file[32];
  char *rgba, *yuv;
  FILE *terminal;

  unsigned long yuv_w;
  unsigned long yuv_h;

  unsigned char *yuv_y;
  unsigned char *yuv_u;
  unsigned char *yuv_v;

  unsigned int x;
  unsigned int y;

  int w = 720, h = 400;

  snprintf(terminal_file, 32, "terminal-%02d.rgba", i);
  terminal = fopen(terminal_file, "r");
  if (!terminal) {
    printf("could not open %s\n", terminal_file);
    exit(1);
  }

  rgba = malloc(sizeof(unsigned char) * 720* 400 * 4);
        // 720 * 400 * 4 is the size of terminal file
  if (!rgba) {
    printf("could not malloc rgba 720*400*4 for %s\n", terminal_file);
    exit(1);
  }

  yuv = malloc(sizeof(unsigned char) * 720* 400 * 3);
  if (!yuv) {
    printf("could not malloc yuv 720*400*3 for %s\n", terminal_file);
    exit(1);
  }

  if (fread(rgba, 1, 720*400*4, terminal) != 720*400*4) {
    printf("failed reading 720*400*4 bytes from %s\n", terminal_file);
    exit(1);
  }

  rgba_to_yuv(rgba, yuv, 720, 400);

  // Must hold: yuv_w >= w 
  yuv_w = (w + 15) & ~15;

  // Must hold: yuv_h >= h 
  yuv_h = (h + 15) & ~15;

  ycbcr[0].width = yuv_w;
  ycbcr[0].height = yuv_h;
  ycbcr[0].stride = yuv_w;
  ycbcr[1].width = (chroma_format == TH_PF_444) ? yuv_w : (yuv_w >> 1);
  ycbcr[1].stride = ycbcr[1].width;
  ycbcr[1].height = (chroma_format == TH_PF_420) ? (yuv_h >> 1) : yuv_h;
  ycbcr[2].width = ycbcr[1].width;
  ycbcr[2].stride = ycbcr[1].stride;
  ycbcr[2].height = ycbcr[1].height;

  ycbcr[0].data = yuv_y = malloc(ycbcr[0].stride * ycbcr[0].height);
  ycbcr[1].data = yuv_u = malloc(ycbcr[1].stride * ycbcr[1].height);
  ycbcr[2].data = yuv_v = malloc(ycbcr[2].stride * ycbcr[2].height);

  for(y = 0; y < h; y++) {
    for(x = 0; x < w; x++) {
      yuv_y[x + y * yuv_w] = yuv[3 * (x + y * w) + 0];
    }
  }

  if (chroma_format == TH_PF_420) {
    for(y = 0; y < h; y += 2) {
      for(x = 0; x < w; x += 2) {
        yuv_u[(x >> 1) + (y >> 1) * (yuv_w >> 1)] =
          yuv[3 * (x + y * w) + 1];
        yuv_v[(x >> 1) + (y >> 1) * (yuv_w >> 1)] =
          yuv[3 * (x + y * w) + 2];
      }
    }
  } else if (chroma_format == TH_PF_444) {
    for(y = 0; y < h; y++) {
      for(x = 0; x < w; x++) {
        yuv_u[x + y * ycbcr[1].stride] = yuv[3 * (x + y * w) + 1];
        yuv_v[x + y * ycbcr[2].stride] = yuv[3 * (x + y * w) + 2];
      }
    }
  } else {  // TH_PF_422 
    for(y = 0; y < h; y += 1) {
      for(x = 0; x < w; x += 2) {
        yuv_u[(x >> 1) + y * ycbcr[1].stride] =
          yuv[3 * (x + y * w) + 1];
        yuv_v[(x >> 1) + y * ycbcr[2].stride] =
          yuv[3 * (x + y * w) + 2];
      }
    }    
  }

  if(th_encode_ycbcr_in(td, ycbcr)) {
    fprintf(stderr, "error: could not encode frame\n");
    return 1;
  }

  if(!th_encode_packetout(td, last, &op)) {
    fprintf(stderr, "error: could not read packets\n");
    return 1;
  }

  ogg_stream_packetin(ogg_os, &op);
  while(ogg_stream_pageout(ogg_os, &og)) {
    fwrite(og.header, og.header_len, 1, ogg_fp);
    fwrite(og.body, og.body_len, 1, ogg_fp);
  }

  free(yuv_y);
  free(yuv_u);
  free(yuv_v);

  free(yuv);
  free(rgba);
  fclose(terminal);

  return 0;
}
 
int main() {
    th_info ti;
    th_enc_ctx *td;
    th_comment tc;
    ogg_packet op;
    ogg_page og;
    ogg_stream_state ogg_os;

    th_info_init(&ti);
    ti.frame_width = ((720 + 15) >> 4) << 4; // ???
    ti.frame_height = ((400 + 15) >> 4) << 4; // ???
    ti.pic_width = 720;
    ti.pic_height = 400;
    ti.pic_x = 0;
    ti.pic_y = 0;

    ti.fps_numerator = 0; 
    ti.fps_denominator = 0;
    ti.aspect_numerator = 0;
    ti.aspect_denominator = 0;
    ti.colorspace = TH_CS_UNSPECIFIED;
    ti.pixel_fmt = TH_PF_420;
    ti.target_bitrate = 0;
    ti.quality = 10; // quality from 0-63
    ti.keyframe_granule_shift=6; // ???

    td = th_encode_alloc(&ti);  
    th_info_clear(&ti);

    srand((getpid()<<16) ^ (getpid()<<8) ^ time(NULL));

    FILE *ogg_fp=fopen("video.ogv", "w+");
    if (!ogg_fp) {
        printf("failed opening video.ogv\n");
        exit(1);
    }
    if (ogg_stream_init(&ogg_os, rand())) {
        printf("could not create ogg stream state\n");
        exit(1);
    }

    th_comment_init(&tc);
    if (th_encode_flushheader(td, &tc, &op) <= 0) {
        printf("could not th_encode_flushheader\n");
        exit(1);
    }
    th_comment_clear(&tc);

    ogg_stream_packetin(&ogg_os, &op);
    if (ogg_stream_pageout(&ogg_os, &og)!=1) {
        printf("could not ogg_stream_pageout\n");
        exit(1);
    }
    fwrite(og.header,1,og.header_len,ogg_fp);
    fwrite(og.body,1,og.body_len,ogg_fp);

    for (;;) {
        int ret = th_encode_flushheader(td, &tc, &op);
        if (ret<0) {
            printf("could not th_encode_flushheader 2\n");
            exit(1);
        }
        else if (ret == 0) break;
        ogg_stream_packetin(&ogg_os, &op);
    }

    for (;;) {
        int ret = ogg_stream_flush(&ogg_os, &og);
        if (ret < 0) {
            printf("could not ogg_stream_flush\n");
            exit(1);
        }
        else if (ret == 0) break;
        fwrite(og.header, 1, og.header_len, ogg_fp);
        fwrite(og.body, 1, og.body_len, ogg_fp);
    }

    int i;
    for (i=0; i<=23; i++) { // 23 terminal rgba buffers
        printf("doing frame %02d\n", i);
        theora_write_frame(i, ogg_fp, &ogg_os, td, i==23);
    }

    fclose(ogg_fp);
    ogg_stream_clear(&ogg_os);

    return 0;
}

