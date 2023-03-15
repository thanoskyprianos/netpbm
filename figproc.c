/*

I recommend creating two executables:

gcc -DTRANSFORM -o figproc_transform figproc.c
gcc -DCONVERT   -o figproc_convert   figproc.c

TRANSFROM is used for P6 -> P5 -> P4 and P3 -> P2 -> P1  conversions
CONVERT   is used for P6 <-> P3, P5 <-> P2 and P4 <-> P1 conversions

either -DTRANSFORM or -DCONVERT must be used
but not both of them at the same time

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

#define ERROR -1
#define NOT_COLOR false
#define NOT_FINISHED false

/*Exits the programm if error occured.*/
void check_error(int error){
    if (error){
        fprintf(stderr, "Input error!\n");
        exit(ERROR);
    }
}

/*Checks if we are at the last pixel.*/
bool check_last(int i, int j, int width, int height){
    return (i == height) && (j == width);
}

/*Ignores whitespace (space, \n, \t).*/
int ignore_whitespace(bool finished){
    int ch;
    while (isspace(ch = getchar()) && ch != EOF)
        continue;

    check_error(ch == EOF && !finished); /*finished is used so we don't return error when we have read all color values*/

    ungetc(ch, stdin);                   /*unget last character becuase it is not space*/

    return 0;
}

/*Ignores comments in the form of '#...\n'.*/
void ignore_comments(void){
    int ch;
    bool found_hash = 0;

    ignore_whitespace(NOT_FINISHED);

    while ((ch = getchar()) != EOF){
        if (ch == '#')
            found_hash = 1;
        else if (ch == '\n'){
            found_hash = 0;

            /*if newline is found we procced to check if the next character is valid or not*/
            /*the character is valid if its a digit (first digit of width) or a '# (new comment)*/
            ignore_whitespace(NOT_FINISHED);
            ch = getchar();
        }

        if (!found_hash){
            if (isdigit(ch)) break; /*checks if ch is the first digit of the width*/
            else if (ch == '#'){    /*checks if there are more comments*/
                ungetc(ch, stdin);  /*unget ch because it's the start of the next comment*/
                continue;
            }
            check_error(ERROR);     /*if none of the above the ch is a not allowed character*/
        }
    }
    check_error(ch == EOF);         /*if file ends prematurely*/

    ungetc(ch, stdin);              /*unget last character because it is the first digit of the width*/
}

/*Calculates luminosity used to convert from colored to gray scale.*/
int luminosity(int r, int g, int b){
    return (299 * r + 587 * g + 114 * b) / 1000;
}

/*Returns a number that is represented in binary.*/
int get_binary_num(int max_color){
    int ch = getchar();

    check_error(ch == EOF);
    check_error(ch > max_color);

    return ch;
}

/*Returns a number that is represented in ascii.*/
int get_ascii_num(int max_color, bool finished){
    int ch, num = 0;
    while (!isspace(ch = getchar())){
        check_error(!isdigit(ch));

        num = (num * 10) + (ch - '0'); /*ascci representation of number -> integer number*/
    }

    check_error(ch == EOF);

    /*check if max_color is true because the function is also called*/
    /*by properties of file(magic nubmer, height, width, max_color)*/
    check_error(max_color && num > max_color);

    ignore_whitespace(finished);

    return num;
}

/*P5 -> P4*/
void to_bnw_binary(int width, int height, int max_color){
    int i, j, k, color, byte, upper_bound;
    for (i = 1; i <= height; i++){
        for (j = 1; j <= width; j += 8){

            /*1111 1111, even if we dont have 8 numbers left in the line, the remaining bits are already 1*/
            byte = 0xFF;

            /*used to check if there are 8 numbers on the last pair of the line*/
            upper_bound = width - j + 1;
            if (upper_bound >= 8) upper_bound = 8;

            for (k = 1; k <= upper_bound; k++){
                color = get_binary_num(max_color);

                /*XOR the byte with 0000 0001 shifted in the correct place to put 0 where needed*/
                if (color > (max_color + 1) / 2) byte ^= 1 << (8 - k);
            }
            putchar(byte);
        }
    }
}

/*P2 -> P1*/
void to_bnw_ascii(int width, int height, int max_color){
    int ch, color;
    bool is_last;

    for (int i = 1; i <= height; i++){
        for (int j = 1; j <= width; j++){
            is_last = check_last(i, j, width, height);

            color = get_ascii_num(max_color, is_last);

            if (color > (max_color + 1) / 2) printf("0 ");
            else                             printf("1 ");
        }
        putchar('\n');
    }
}

/*P6 -> P5*/
void to_gray_scale_binary(int width, int height, int max_color){
    int r, g, b;

    for (int i = 1; i <= height; i++){
        for (int j = 1; j <= width; j++){
            r = get_binary_num(max_color);
            g = get_binary_num(max_color);
            b = get_binary_num(max_color);

            putchar(luminosity(r, g, b));
        }
    }
}

/*P3 -> P2*/
void to_gray_scale_ascii(int width, int height, int max_color)
{
    int r, g, b;
    bool is_last;
    for (int i = 1; i <= height; i++){
        for (int j = 1; j <= width; j++){
            is_last = check_last(i, j, width, height);

            r = get_ascii_num(max_color, is_last);
            g = get_ascii_num(max_color, is_last);
            b = get_ascii_num(max_color, is_last);

            printf("%3d ", luminosity(r, g, b));
        }
        putchar('\n');
    }
}

/*P4 -> P1*/
void binary_to_ascii_bnw(int width, int height, int max_color)
{
    int i, j, color, k, upper_bound;
    for (i = 1; i <= height; i++){
        for (j = 1; j <= width; j += 8){
            color = get_binary_num(max_color);

            /*used to check if there are 8 bytes on the last pair of the line*/
            upper_bound = width - j + 1;
            if (upper_bound >= 8) upper_bound = 8;

            /*each bit of color is a color value*/
            /*we logical AND it with 0000 0001 shifted to the correct place*/
            /*if the result is true (byte with 1 in the specific place and 0 everywhere else),
            /*the bit is black (1), else (all bits 0) the bit is white(0)*/
            for (k = 1; k <= upper_bound; k++)
                printf("%d ", (color & (1 << (8 - k))) ? 1 : 0);
        }
        putchar('\n');
    }
}

/*P6 -> P3 and P5 -> P2*/
/*For colored images we pass 3 * width because of the r g b values*/
void binary_to_ascii_cg(int width, int height, int max_color)
{
    int i, j, color;
    for (i = 1; i <= height; i++){
        for (j = 1; j <= width; j++){
            color = get_binary_num(max_color);
            printf("%3d ", color);
        }
        putchar('\n');
    }
}

/*P1 -> P4*/
void ascii_to_binary_bnw(int width, int height, int max_color){
    int i, j, k, color, byte, upper_bound;
    bool is_last;
    for(i = 1; i <= height; i++){
        for(j = 1; j <= width; j+=8){

            /*1111 1111, even if we dont have 8 numbers left in the line, the remaining bits are already 1*/
            byte = 0xFF;

            /*used to check if there are 8 numbers on the last pair of the line*/
            upper_bound = width - j + 1;
            if(upper_bound >= 8) upper_bound = 8;

            for(k = 1; k <= upper_bound; k++){

                /*k is added to j as an offset to check if we are at the last color value*/
                is_last = check_last(i, j + k - 1, width, height);
                color = get_ascii_num(max_color, is_last);

                
                /*if the color is 1 we don't need to do anything as it is already in place*/
                /*if the color is 0 we logical XOR 1111 1111 on the correct position with 0000 0001*/
                /*so we place a 0 in the correct stop of byte*/
                if(color == 0) byte ^= (1 << (8 - k));
            }
            putchar(byte);
        }
    }
}

/*P3 -> P6 and P2 -> P5*/
/*For colored images we pass 3 * width because of the r g b values*/
void ascii_to_binary_cg(int width, int height, int max_color){
    int i, j, color;
    bool is_last;
    for(i = 1; i <= height; i++){
        for(j = 1; j <= width; j++){
            is_last = check_last(i, j, width, height);
            color = get_ascii_num(max_color, is_last);

            putchar(color);
        }
    }
}


int main(void){
    #if defined(TRANSFORM) == defined(CONVERT)
        fprintf(stderr, "Please define TRANSFORM or CONVERT but not both\n");
        return ERROR;
    #endif

    int type, width, height, max_color;

    check_error(getchar() != 'P');

    type = getchar() - '0';
    check_error(type < 1 || type > 6);

    ignore_comments();

    width  = get_ascii_num(NOT_COLOR, NOT_FINISHED);
    height = get_ascii_num(NOT_COLOR, NOT_FINISHED);

    if (type != 1 && type != 4){
        max_color = get_ascii_num(NOT_COLOR, NOT_FINISHED);
        check_error(max_color > 255);
    }

    /*colored -> gray scale -> bnw*/
    #ifdef TRANSFORM

        if(type == 1 || type == 4) 
            check_error(ERROR);          /*cannot proccess bnw image further*/
        else if (type == 2 || type == 5) /*bnw so dont print max color*/
            printf("P%d %d %d\n", type - 1, width, height);
        else                             /*colored*/
            printf("P%d %d %d %d\n", type - 1, width, height, max_color);

        switch (type){
            case 2:  to_bnw_ascii(width, height, max_color);         break;
            case 3:  to_gray_scale_ascii(width, height, max_color);  break;
            case 5:  to_bnw_binary(width, height, max_color);        break;
            case 6:  to_gray_scale_binary(width, height, max_color); break;
        }
    #endif

    /*binary <-> ascii*/
    #ifdef CONVERT
        /*if type is binary subtract 3 to go to ascci*/
        /*if type is ascii  add 3 to go to binary*/
        int new_type;
        if (type > 3) new_type = type - 3;
        else          new_type = type + 3;

        /*check if image is not bnw*/
        if (type != 1 && type != 4)
            printf("P%d %d %d %d\n", new_type, width, height, max_color);
        else
            printf("P%d %d %d\n", new_type, width, height);

        switch (type){
            case 1: ascii_to_binary_bnw(width, height, max_color);    break;
            case 2: ascii_to_binary_cg(width, height, max_color);     break;
            case 3: ascii_to_binary_cg(3 * width, height, max_color); break; /*3 * width because of 3 color values (r, g, b)*/
            case 4: binary_to_ascii_bnw(width, height, max_color);    break;
            case 5: binary_to_ascii_cg(width, height, max_color);     break;
            case 6: binary_to_ascii_cg(3 * width, height, max_color); break; /*3 * width because of 3 color values (r, g, b)*/
        }
    #endif

    fprintf(stderr, "Successful conversion!\n");
    return 0;
}