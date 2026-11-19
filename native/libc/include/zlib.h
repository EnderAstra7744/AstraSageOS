#ifndef _AS_ZLIB_H
#define _AS_ZLIB_H
#include <stddef.h>
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NEED_DICT 2
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR (-3)
#define Z_MEM_ERROR (-4)
#define Z_BUF_ERROR (-5)
#define Z_NO_FLUSH 0
#define Z_FINISH 4
#define Z_NULL 0
#define MAX_WBITS 15
typedef struct z_stream_s {
    const Bytef *next_in; uInt avail_in; uLong total_in;
    Bytef *next_out; uInt avail_out; uLong total_out;
    char *msg; void *state;
    void *zalloc; void *zfree; void *opaque;
} z_stream;
int inflateInit2(z_stream *s, int windowBits);
int inflateInit(z_stream *s);
int inflate(z_stream *s, int flush);
int inflateEnd(z_stream *s);
typedef struct _AS_GZ *gzFile;
gzFile gzopen(const char *path, const char *mode);
char *gzgets(gzFile f, char *buf, int len);
int gzread(gzFile f, void *buf, unsigned len);
int gzclose(gzFile f);
int uncompress(Bytef *dest, uLong *destLen, const Bytef *src, uLong srcLen);
#endif
