/*

Netpbm format converter

I recommend creating two executables:

gcc -DTRANSFORM -o figproc_transform figproc.c
gcc -DCONVERT   -o figproc_convert   figproc.c

TRANSFORM is used for P6 -> P5 -> P4 and P3 -> P2 -> P1 conversions
CONVERT   is used for P6 <-> P3, P5 <-> P2 and P4 <-> P1 conversions

Where:
* P1:  B&W Ascii
* P2:  Grayscale Ascii
* P3:  RGB Ascii
* P4:  B&W Binary
* P5:  Grayscale Binary
* P6:  RGB Binary

Either -DTRANSFORM or -DCONVERT must be used,
but not both of them at the same time.

Usage:

figproc_transform < image.pnm > converted.pnm
figproc_convert   < image.pnm > converted.pnm

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

#define EXIT_ERROR 1
#define IO_CHUNK (1u << 16)   /*bulk read/write chunk size in bytes*/

/*image format types*/
enum { P1 = 1, P2 = 2, P3 = 3, P4 = 4, P5 = 5, P6 = 6 };

/*Exits the program if an error occurred.*/
void check_error(bool failed, const char *message){
    if (failed){
        fprintf(stderr, "figproc: error: %s\n", message);
        exit(EXIT_ERROR);
    }
}

/*---------------------------------------------------------------------------*/
/*output buffering                                                           */
/*---------------------------------------------------------------------------*/

static unsigned char outbuf[IO_CHUNK];
static size_t outlen = 0;

/*Writes one byte to the output, flushing the buffer when full.*/
static void out_byte(unsigned int value){
    outbuf[outlen++] = (unsigned char)value;
    if (outlen == sizeof outbuf){
        fwrite(outbuf, 1, outlen, stdout);
        outlen = 0;
    }
}

/*Writes a NUL-terminated string to the output.*/
static void out_str(const char *str){
    for (; *str; str++)
        out_byte((unsigned char)*str);
}

/*Flushes the output buffer.*/
static void out_flush(void){
    fwrite(outbuf, 1, outlen, stdout);
    outlen = 0;
}

/*Bit writer state for P4 output (most significant bit first).*/
static unsigned int bit_byte = 0;
static unsigned int bit_count = 0;

/*Adds one pixel to the P4 bit stream (1 = black, 0 = white).*/
static void out_bit(unsigned int black){
    if (black)
        bit_byte |= 0x80u >> bit_count;

    bit_count++;
    if (bit_count == 8){
        out_byte(bit_byte);
        bit_byte = 0;
        bit_count = 0;
    }
}

/*Completes the current P4 row, flushing the partial byte with zero padding.*/
static void out_bit_end_row(void){
    if (bit_count){
        out_byte(bit_byte);
        bit_byte = 0;
        bit_count = 0;
    }
}

/*---------------------------------------------------------------------------*/
/*input parsing                                                              */
/*---------------------------------------------------------------------------*/

/*Skips whitespace and comment lines ('#' up to end of line).
  Comments may legally appear anywhere in the header of every format
  and anywhere in the raster of the ASCII formats.*/
void skip_whitespace_and_comments(void){
    int ch;

    for (;;){
        ch = getchar();
        if (ch == '#'){
            do
                ch = getchar();
            while (ch != '\n' && ch != '\r' && ch != EOF);
            check_error(ch == EOF, "unterminated comment at end of input");
        }
        else if (ch != EOF && isspace(ch))
            continue;
        else{
            check_error(ch == EOF, "unexpected end of input");
            ungetc(ch, stdin);
            return;
        }
    }
}

/*Reads one decimal number.  max_color is the largest legal value
  (0 means unlimited).  If allow_eof is true, end of input directly after
  the number is accepted (last value of the raster).  The terminating
  character is left unconsumed so that the caller can decide how to
  handle it.*/
int get_ascii_num(int max_color, bool allow_eof){
    int ch, num = 0;

    skip_whitespace_and_comments();
    ch = getchar();
    check_error(ch == EOF, "unexpected end of input where number was expected");
    check_error(!isdigit(ch), "expected a decimal number, found invalid character");

    do{
        check_error(num > 99999999, "number in input is too large");
        num = num * 10 + (ch - '0');  /*ascii representation of number -> integer*/
        ch = getchar();
    } while (ch != EOF && isdigit(ch));

    if (ch == EOF)
        check_error(!allow_eof, "unexpected end of input after number");
    else{
        check_error(!isspace(ch) && ch != '#',
                    "expected whitespace after number, found invalid character");
        ungetc(ch, stdin);
    }

    check_error(max_color && num > max_color,
                "value is larger than the maximum color value");
    return num;
}

/*Positions the input stream at the first byte of a binary (P4/P5/P6) raster.
  A single whitespace character separates the last header value from the
  raster, and comment lines may appear before it.  A comment ends just
  before its terminating newline, so that newline itself is whitespace and
  may act as the raster delimiter.  Only the delimiter is consumed here;
  the first raster byte is never eaten, even if it happens to be a
  whitespace character value.*/
void skip_to_binary_raster(void){
    int ch;

    for (;;){
        ch = getchar();
        check_error(ch == EOF, "unexpected end of input in header");

        for (;;){
            if (ch == '#'){
                /*skip the comment content; its terminating newline (left
                  in ch) is whitespace and may be the raster delimiter*/
                do
                    ch = getchar();
                while (ch != '\n' && ch != '\r' && ch != EOF);
                check_error(ch == EOF, "unterminated comment at end of input");

                if (ch == '\r'){
                    /*merge a CRLF line ending into one delimiter unit*/
                    int ch2 = getchar();
                    if (ch2 != '\n' && ch2 != EOF)
                        ungetc(ch2, stdin);
                    else
                        ch = '\n';
                }
            }

            if (!isspace(ch)){
                /*lenient: no delimiter whitespace, this byte is already raster data*/
                ungetc(ch, stdin);
                return;
            }

            /*whitespace: a delimiter candidate, unless a comment follows
              directly, in which case it merely led into the comment*/
            ch = getchar();
            check_error(ch == EOF, "unexpected end of input in header");
            if (ch == '#')
                continue;

            /*the delimiter; the first raster byte is left unconsumed*/
            if (ch != EOF)
                ungetc(ch, stdin);
            return;
        }
    }
}

/*Reads count raw raster bytes from stdin into dst.*/
void read_raster_bytes(unsigned char *dst, size_t count){
    size_t done = 0;

    while (done < count){
        size_t got = fread(dst + done, 1, count - done, stdin);
        check_error(got == 0, "unexpected end of input in raster data");
        done += got;
    }
}

/*Reads the image header and leaves the input positioned at the first
  raster byte.  Returns the format type (P1 to P6).*/
int read_header(int *width, int *height, int *max_color){
    int type;

    check_error(getchar() != 'P', "input does not start with the magic number 'P'");

    type = getchar() - '0';
    check_error(type < 1 || type > 6, "unsupported magic number (expected P1 to P6)");

    *width  = get_ascii_num(0, false);
    *height = get_ascii_num(0, false);

    check_error(*width  < 1, "image width must be at least 1");
    check_error(*height < 1, "image height must be at least 1");

    if (type == P1 || type == P4)
        *max_color = 1;
    else
        *max_color = get_ascii_num(0, false);

    if (type != P1 && type != P4)
        check_error(*max_color < 1 || *max_color > 255,
                    "maximum color value must be between 1 and 255");

    /*a single whitespace character separates the header of a binary image
      from its raster; comments may appear before it*/
    if (type == P4 || type == P5 || type == P6)
        skip_to_binary_raster();

    return type;
}

/*---------------------------------------------------------------------------*/
/*color math                                                                 */
/*---------------------------------------------------------------------------*/

/*Converts red/green/blue components to one grayscale value
  (0.299 R + 0.587 G + 0.114 B, rounded).*/
int luminosity(int r, int g, int b){
    return (299 * r + 587 * g + 114 * b + 500) / 1000;
}

/*Prints the header of the produced image.*/
void print_header(int type, int width, int height, int max_color){
    if (type == P1 || type == P4)
        fprintf(stdout, "P%d\n%d %d\n", type, width, height);
    else
        fprintf(stdout, "P%d\n%d %d\n%d\n", type, width, height, max_color);
}

/*---------------------------------------------------------------------------*/
/*TRANSFORM: color -> grayscale -> black & white                             */
/*---------------------------------------------------------------------------*/

/*P6 -> P5*/
void to_gray_scale_binary(int width, int height, int max_color){
    const size_t npix = (size_t)width * height;
    unsigned char buf[IO_CHUNK];
    size_t done = 0;

    while (done < npix){
        size_t want = npix - done;
        if (want > sizeof buf / 3) want = sizeof buf / 3;

        read_raster_bytes(buf, want * 3);

        for (size_t k = 0; k < want; k++){
            int r = buf[3 * k], g = buf[3 * k + 1], b = buf[3 * k + 2];
            check_error(r > max_color || g > max_color || b > max_color,
                        "raster value exceeds the maximum color value");
            out_byte(luminosity(r, g, b));
        }
        done += want;
    }
}

/*P5 -> P4*/
void to_bnw_binary(int width, int height, int max_color){
    const int threshold = (max_color + 1) / 2;
    const size_t npix = (size_t)width * height;
    unsigned char buf[IO_CHUNK];
    size_t done = 0;

    while (done < npix){
        size_t want = npix - done;
        if (want > sizeof buf) want = sizeof buf;

        read_raster_bytes(buf, want);

        for (size_t k = 0; k < want; k++){
            check_error(buf[k] > max_color,
                        "raster value exceeds the maximum color value");

            /*darker than the threshold becomes black (1), lighter becomes white (0)*/
            out_bit(buf[k] <= threshold);

            if ((done + k + 1) % width == 0)
                out_bit_end_row();
        }
        done += want;
    }
}

/*P2 -> P1*/
void to_bnw_ascii(int width, int height, int max_color){
    const int threshold = (max_color + 1) / 2;
    const size_t npix = (size_t)width * height;

    for (size_t n = 0; n < npix; n++){
        bool is_last = (n == npix - 1);
        int color = get_ascii_num(max_color, is_last);

        if (n % width != 0) out_byte(' ');
        out_byte(color <= threshold ? '1' : '0');

        if ((n + 1) % width == 0) out_byte('\n');
    }
}

/*P3 -> P2*/
void to_gray_scale_ascii(int width, int height, int max_color){
    const size_t npix = (size_t)width * height;
    char str[16];

    for (size_t n = 0; n < npix; n++){
        bool is_last = (n == npix - 1);
        int r = get_ascii_num(max_color, is_last);
        int g = get_ascii_num(max_color, is_last);
        int b = get_ascii_num(max_color, is_last);

        if (n % width != 0) out_byte(' ');
        sprintf(str, "%d", luminosity(r, g, b));
        out_str(str);

        if ((n + 1) % width == 0) out_byte('\n');
    }
}

/*---------------------------------------------------------------------------*/
/*CONVERT: binary <-> ASCII                                                  */
/*---------------------------------------------------------------------------*/

/*P4 -> P1*/
void binary_to_ascii_bnw(int width, int height){
    const size_t rowbytes = ((size_t)width + 7) / 8;
    const size_t total = rowbytes * height;
    unsigned char buf[IO_CHUNK];
    size_t done = 0;
    size_t bit_index = 0;   /*pixel position within the current row*/

    while (done < total){
        size_t want = total - done;
        if (want > sizeof buf) want = sizeof buf;

        read_raster_bytes(buf, want);

        for (size_t k = 0; k < want; k++){
            for (int bit = 7; bit >= 0; bit--){
                if (bit_index >= (size_t)width)
                    break;  /*skip the padding bits of the row*/

                if (bit_index != 0) out_byte(' ');
                /*each bit of the byte is a pixel value: 1 is black, 0 is white*/
                out_byte((buf[k] >> bit) & 1 ? '1' : '0');
                bit_index++;
            }
            if (bit_index == (size_t)width){
                out_byte('\n');
                bit_index = 0;
            }
        }
        done += want;
    }
}

/*P5 -> P2 and P6 -> P3*/
/*For colored images pass 3 * width because of the r g b values.*/
void binary_to_ascii_cg(int width, int height, int max_color){
    const size_t npix = (size_t)width * height;
    unsigned char buf[IO_CHUNK];
    char str[16];
    size_t done = 0;

    while (done < npix){
        size_t want = npix - done;
        if (want > sizeof buf) want = sizeof buf;

        read_raster_bytes(buf, want);

        for (size_t k = 0; k < want; k++){
            check_error(buf[k] > max_color,
                        "raster value exceeds the maximum color value");

            if ((done + k) % width != 0) out_byte(' ');
            sprintf(str, "%3u", (unsigned int)buf[k]);
            out_str(str);

            if ((done + k + 1) % width == 0) out_byte('\n');
        }
        done += want;
    }
}

/*P1 -> P4*/
void ascii_to_binary_bnw(int width, int height){
    const size_t npix = (size_t)width * height;

    for (size_t n = 0; n < npix; n++){
        bool is_last = (n == npix - 1);
        int color = get_ascii_num(1, is_last);  /*values must be 0 or 1*/

        out_bit(color == 1);

        if ((n + 1) % width == 0)
            out_bit_end_row();
    }
}

/*P2 -> P5 and P3 -> P6*/
/*For colored images pass 3 * width because of the r g b values.*/
void ascii_to_binary_cg(int width, int height, int max_color){
    const size_t npix = (size_t)width * height;

    for (size_t n = 0; n < npix; n++){
        bool is_last = (n == npix - 1);
        out_byte(get_ascii_num(max_color, is_last));
    }
}

/*---------------------------------------------------------------------------*/
/*main                                                                       */
/*---------------------------------------------------------------------------*/

int main(void){
    #if defined(TRANSFORM) == defined(CONVERT)
        fprintf(stderr, "Please define TRANSFORM or CONVERT but not both\n");
        return EXIT_ERROR;
    #endif

    int type, width, height, max_color;

    type = read_header(&width, &height, &max_color);

    #ifdef TRANSFORM
        check_error(type == P1 || type == P4,
                    "cannot transform a black-and-white image further");

        print_header(type - 1, width, height, max_color);

        switch (type){
            case P2: to_bnw_ascii(width, height, max_color);         break;
            case P3: to_gray_scale_ascii(width, height, max_color);  break;
            case P5: to_bnw_binary(width, height, max_color);        break;
            case P6: to_gray_scale_binary(width, height, max_color); break;
        }
    #endif

    #ifdef CONVERT
        /*if the type is binary subtract 3 to go to ascii*/
        /*if the type is ascii  add 3 to go to binary*/
        int new_type = (type > 3) ? type - 3 : type + 3;

        print_header(new_type, width, height, max_color);

        switch (type){
            case P1: ascii_to_binary_bnw(width, height);                break;
            case P2: ascii_to_binary_cg(width, height, max_color);      break;
            case P3: ascii_to_binary_cg(3 * width, height, max_color);  break; /*3 * width because of 3 color values (r, g, b)*/
            case P4: binary_to_ascii_bnw(width, height);                break;
            case P5: binary_to_ascii_cg(width, height, max_color);      break;
            case P6: binary_to_ascii_cg(3 * width, height, max_color);  break; /*3 * width because of 3 color values (r, g, b)*/
        }
    #endif

    out_flush();
    fflush(stdout);

    fprintf(stderr, "Successful conversion!\n");
    return 0;
}
