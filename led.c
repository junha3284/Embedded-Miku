#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include "led.h"

#define FILE_NAME_EXPORT "/sys/class/gpio/export"
#define FILE_NAME_UNEXPORT "/sys/class/gpio/unexport"

#define GPIO_RED1 86 // Upper
#define GPIO_BLUE1 87 
#define GPIO_GREEN1 88 
#define GPIO_RED2 10 // Under
#define GPIO_BLUE2 9 
#define GPIO_GREEN2 11 

#define GPIO_ROW_A 8 
#define GPIO_ROW_B 80 
#define GPIO_ROW_C 78 

#define GPIO_LATCH 77 
#define GPIO_CLOCK 76  
#define GPIO_OE 74 

#define BUFF_SIZE 64 
#define SLOT_NUM  4

#define LED_COLUM_NUM 32
#define LED_ROW_NUM 16

static int fileDesc_red1;
static int fileDesc_blue1;
static int fileDesc_green1;

static int fileDesc_red2;
static int fileDesc_blue2;
static int fileDesc_green2;

static int fileDesc_row_a;
static int fileDesc_row_b;
static int fileDesc_row_c;

static int fileDesc_clock;
static int fileDesc_latch;
static int fileDesc_oe;

struct timespec reqtime;

// LED values for display
static int screen[LED_COLUM_NUM][LED_ROW_NUM];

static pthread_t refreshThread;
static pthread_mutex_t screenLock = PTHREAD_MUTEX_INITIALIZER;
static bool running;

// function declartionas ----------------------------
// Beaglebone GPIO
static int fileDesc_opener(int gpio_num);
static void GPIO_setPins(void);
static void GPIO_export_out(int gpio_num);

// control LED
static void LED_setPixel(int r, int c, int color);
static void LED_clock(void);
static void LED_latch(void);
static void LED_bitsFromInt(int * arr, int input);
static void LED_setRow(int rowNum);
static void LED_setColourTop(int colour);
static void LED_setColourBottom(int colour);
static void LED_refresh(void);
static void* refreshLoop (void*);

int LED_init (void)
{
    GPIO_setPins();
    for( int i = 0; i < LED_COLUM_NUM; i++)
        for (int j = 0; j < LED_ROW_NUM; j++)
                screen[i][j] = 0;

    running = true;
    int threadCreateResult = pthread_create( &refreshThread, NULL, refreshLoop, NULL);
    return threadCreateResult;
}

// for thread which keeps refreshing LED
static void* refreshLoop (void* empty)
{
    while (running) {
        LED_refresh();
    }
    return NULL;
}

void LED_cleanup(void)
{
    running = false;
    pthread_join (refreshThread, NULL);
    LED_clean_display();
    LED_refresh();
    return;
}

// display rectangle on LED
// @params:
//      two vertexs for diagonal of the rectangle
//      (x1, y1) : one of Vertex for rectangle
//      (x2, y2) : the other vertex for rectangle 
void LED_display_rectangle(int x1, int y1, int x2, int y2, int color)
{
    assert ( 0 <= x1 && x1 <= LED_COLUM_NUM-1);
    assert ( 0 <= x2 && x2 <= LED_COLUM_NUM-1);
    assert ( 0 <= y1 && y1 <= LED_ROW_NUM-1);
    assert ( 0 <= y2 && y2 <= LED_ROW_NUM-1);


    int min_x;
    int max_x;

    int min_y;
    int max_y;

    if ( x1 < x2 ) {
        min_x = x1;
        max_x = x2;
    }
    else {
        min_x = x2;
        max_x = x1;
    }

    if ( y1 < y2 ){
        min_y = y1;
        max_y = y2;
    }
    else {
        min_y = y2;
        max_y = y1;
    }

    for (int i = min_x; i <=  max_x; i++ )
        for (int j = min_y; j <= max_y; j++)
            LED_setPixel(i,j,color);

    return;
}

// Clean up LED 
void LED_clean_display()
{
    LED_display_rectangle(0,0, LED_COLUM_NUM-1, LED_ROW_NUM-1,0);
}

static int fileDesc_opener(int gpio_num)
{
    char buf[BUFF_SIZE];
    snprintf(buf, BUFF_SIZE, "/sys/class/gpio/gpio%d/value", gpio_num);
    return open(buf, O_RDWR);
}

//////////////////////////////////////////////////////////////////////////////////////////////
//                                          GPIO                                            //
//////////////////////////////////////////////////////////////////////////////////////////////
// Set up GPIO pins for LED (export and set direction to out)
static void GPIO_setPins(void)
{
    // LED UPPER
    GPIO_export_out(GPIO_RED1);
    fileDesc_red1 = fileDesc_opener(GPIO_RED1);

    GPIO_export_out(GPIO_GREEN1);
    fileDesc_green1 = fileDesc_opener(GPIO_GREEN1);

    GPIO_export_out(GPIO_BLUE1);
    fileDesc_blue1 = fileDesc_opener(GPIO_BLUE1);

    // LED UNDER
    GPIO_export_out(GPIO_RED2);
    fileDesc_red2 = fileDesc_opener(GPIO_RED2);

    GPIO_export_out(GPIO_GREEN2);
    fileDesc_green2 = fileDesc_opener(GPIO_GREEN2);

    GPIO_export_out(GPIO_BLUE2);
    fileDesc_blue2 = fileDesc_opener(GPIO_BLUE2);

    // ROW
    GPIO_export_out(GPIO_ROW_A);
    fileDesc_row_a = fileDesc_opener(GPIO_ROW_A);

    GPIO_export_out(GPIO_ROW_B);
    fileDesc_row_b = fileDesc_opener(GPIO_ROW_B);

    GPIO_export_out(GPIO_ROW_C);
    fileDesc_row_c = fileDesc_opener(GPIO_ROW_C);

    // Timing
    GPIO_export_out(GPIO_CLOCK);
    fileDesc_clock = fileDesc_opener(GPIO_CLOCK);

    GPIO_export_out(GPIO_LATCH);
    fileDesc_latch = fileDesc_opener(GPIO_LATCH);

    GPIO_export_out(GPIO_OE);
    fileDesc_oe = fileDesc_opener(GPIO_OE);
    return;
}

static void GPIO_export_out (int gpio_num)
{
    FILE *export = fopen (FILE_NAME_EXPORT, "w");
    assert(export != NULL);

    int charWritten = fprintf (export, "%d", gpio_num);
    assert (charWritten > 0);
    fclose (export);

    char buf[BUFF_SIZE];
    snprintf(buf, BUFF_SIZE, "/sys/class/gpio/gpio%d/direction", gpio_num);
    FILE *pGpioDirection = fopen(buf, "w");
    charWritten = fprintf(pGpioDirection, "%s", "out");
    assert(charWritten > 0 );
    fclose (pGpioDirection);
}

//////////////////////////////////////////////////////////////////////////////////////////////
//                                              LED                                         //
//////////////////////////////////////////////////////////////////////////////////////////////
/*
* Set the pixel at @r and @c to @color
* @ params:
*      r : row
*      c : column
*/
static void LED_setPixel(int r, int c, int color)
{
    pthread_mutex_lock (&screenLock);
    {
        screen[r][c] = color;
    }
    pthread_mutex_unlock (&screenLock);
    return;
}

/** 
 *  Generate the clock pins
 */
static void LED_clock(void)
{
    // Bit-bang the clock gpio
    // Notes: Before program writes, must make sure it's on the 0 index
    lseek(fileDesc_clock, 0, SEEK_SET);
    write(fileDesc_clock, "1", 1);
    lseek(fileDesc_clock, 0, SEEK_SET);
    write(fileDesc_clock, "0", 1);

    return;
}

/**
 *  Generate the latch pins
 */
static void LED_latch(void)
{
    lseek(fileDesc_latch, 0, SEEK_SET);
    write(fileDesc_latch, "1", 1);
    lseek(fileDesc_latch, 0, SEEK_SET);
    write(fileDesc_latch, "0", 1);

    return;
}

/**
 *  Convert integer passed on into bits and put in array
 *  @params:
 *      int * arr: pointer to array to be filled with bits
 *      int input: integer to be converted to bits
 */
static void LED_bitsFromInt(int * arr, int input)
{
    arr[0] = input & 1;

    arr[1] = input & 2;
    arr[1] = arr[1] >> 1;

    arr[2] = input & 4;
    arr[2] = arr[2] >> 2;

    return;
}

/**
 *  Set LED Matrix row
 *  @params:
 *      int rowNum: the rowNumber to be inputted to row pins
 */
static void LED_setRow(int rowNum)
{
    // Convert rowNum single bits from int
    int arr[3] = {0, 0, 0};
    LED_bitsFromInt(arr, rowNum);

    // Write on the row pins
    char a_val[2];
    sprintf(a_val, "%d", arr[0]);
    lseek(fileDesc_row_a, 0, SEEK_SET);
    write(fileDesc_row_a, a_val, 1);

    char b_val[2];
    sprintf(b_val, "%d", arr[1]);
    lseek(fileDesc_row_b, 0, SEEK_SET);
    write(fileDesc_row_b, b_val, 1);

    char c_val[2];
    sprintf(c_val, "%d", arr[2]);
    lseek(fileDesc_row_c, 0, SEEK_SET);
    write(fileDesc_row_c, c_val, 1);


    return;
}

/**
 *  Set the colour of the top part of the LED
 *  @params:
 *      int colour: colour to be set
 */
static void LED_setColourTop(int colour)
{
    int arr[3] = {0, 0, 0};
    LED_bitsFromInt(arr, colour);

    // Write on the colour pins
    char red1_val[2];
    sprintf(red1_val, "%d", arr[0]);
    lseek(fileDesc_red1, 0, SEEK_SET);
    write(fileDesc_red1, red1_val, 1);

    char green1_val[2];
    sprintf(green1_val, "%d", arr[1]);
    lseek(fileDesc_green1, 0, SEEK_SET);
    write(fileDesc_green1, green1_val, 1);

    char blue1_val[2];
    sprintf(blue1_val, "%d", arr[2]);
    lseek(fileDesc_blue1, 0, SEEK_SET);
    write(fileDesc_blue1, blue1_val, 1);    

    return;
}

/**
 *  Set the colour of the bottom part of the LED
 *  @params:
 *      int colour: colour to be set
 */
static void LED_setColourBottom(int colour)
{
    int arr[3] = {0,0,0};
    LED_bitsFromInt(arr, colour);

    // Write on the colour pins
    char red2_val[2];
    sprintf(red2_val, "%d", arr[0]);
    lseek(fileDesc_red2, 0, SEEK_SET);
    write(fileDesc_red2, red2_val, 1);

    char green2_val[2];
    sprintf(green2_val, "%d", arr[1]);
    lseek(fileDesc_green2, 0, SEEK_SET);
    write(fileDesc_green2, green2_val, 1);

    char blue2_val[2];
    sprintf(blue2_val, "%d", arr[2]);
    lseek(fileDesc_blue2, 0, SEEK_SET);
    write(fileDesc_blue2, blue2_val, 1);      

    return;
}

/**
 *  Fill the LED Matrix with the respective pixel colour
 */
static void LED_refresh(void)
{
    struct timespec reqtime;
    reqtime.tv_sec = 0; 
    reqtime.tv_nsec = 5 * 100000; 
    
    for ( int rowNum = 0; rowNum < 8; rowNum++ ) {
        lseek(fileDesc_oe, 0, SEEK_SET);
        write(fileDesc_oe, "1", 1); 
        LED_setRow(rowNum);
        for ( int colNum = 0; colNum < 32;  colNum++) {
            int top;
            int bottom;
            pthread_mutex_lock (&screenLock);
            {
                top = screen[colNum][rowNum];
                bottom = screen[colNum][rowNum+8];
            }
            pthread_mutex_unlock (&screenLock);
            
            LED_setColourTop(top);
            LED_setColourBottom(bottom);
            LED_clock();
        }
        LED_latch();
        lseek(fileDesc_oe, 0, SEEK_SET);
        write(fileDesc_oe, "0", 1); 
        nanosleep(&reqtime, NULL); 
    }
    return;
}

