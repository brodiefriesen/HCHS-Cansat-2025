#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <unistd.h>   // sleep()
#include <stdio.h>
//#include <string.h>
#include <wiringx.h>
#include <stdlib.h>
#include <sys/stat.h>

// Duo:     milkv_duo
// Duo256M: milkv_duo256m
// DuoS:    milkv_duos
#define WIRINGX_TARGET "milkv_duo256m"

int parse_comma_delimited_str(char *string, char **fields, int max_fields)
{
   int i = 0;
   fields[i++] = string;
   while ((i < max_fields) && NULL != (string = strchr(string, ','))) {
      *string = '\0';
      fields[i++] = ++string;
   }
   return --i;
}

//stolen file size function
int fsize(FILE *fp){
    int prev=ftell(fp);
    fseek(fp, 0L, SEEK_END);
    int sz=ftell(fp);
    fseek(fp,prev,SEEK_SET); //go back to where we were
    return sz;
}

int getGPS(struct wiringXSerial_t gpsUart){
    int fd;

    if ((fd = wiringXSerialOpen("/dev/ttyS1", gpsUart)) < 0) {
        fprintf(stderr, "Open serial device failed: 1\n");
        wiringXGC();
        return -1;
    }

    char buf[256];
    char **out;
    int i;
    int recv_len = 0;
    recv_len = wiringXSerialDataAvail(fd);

    if (recv_len > 0) {
        i = 0;
        while (recv_len--)
        {
            buf[i++] = wiringXSerialGetChar(fd);
        }
        parse_comma_delimited_str(buf, out, 8);

        wiringXSerialPrintf(fd, "G:{LAT:{%s%c}:LON{%s%c}:}:", out[2], out[3], out[4], out[5]);
    }

}

int waitOnUart(struct wiringXSerial_t espUart){
    char buf[1024];

    int recv_len = 0;
    int i;
    int fd;

    //open serial connection
    if ((fd = wiringXSerialOpen("/dev/ttyS2", espUart)) < 0) {
        fprintf(stderr, "Open serial device failed: 2\n");
        wiringXGC();
        return -1;
    }
    wiringXSerialFlush(fd);

    //hang until data is recieved
    recv_len = wiringXSerialDataAvail(fd);
    if (recv_len > 0) {
        fprintf(stderr, "recieved!\n");
        i = 0;
        while (recv_len--)
        {
            buf[i++] = wiringXSerialGetChar(fd);
        }
        if (recv_len > 1){
            if(buf[0] == 't'){
                //transmit image instruction
                return 2;
            } else if(buf[0] == 's'){
                //shutdown program instruction
                wiringXSerialPuts(fd, "ACK");
                return 3;
            } else{
                //unrecognized instruction
                wiringXSerialPuts(fd, "?");
                return 1;
            }
        }
    }

    wiringXSerialClose(fd);
    return 0;
}

int transmit(char* filename, struct wiringXSerial_t espUart){
    FILE* fp;
    fp = fopen(filename, "rb");

    int fd;

    int file_size = fsize(fp);

    char buf[file_size];
    int i;
    
    //open serial connection
    if ((fd = wiringXSerialOpen("/dev/ttyS2", espUart)) < 0) {
        fprintf(stderr, "Open serial device failed: 2");
        wiringXGC();
        return -1;
    }

    fprintf(stderr, "filesize: %d\n", file_size);

    //copy image to serial buffer
    fread(buf, 1, file_size, fp);
    write(fd, buf, file_size);

    wiringXSerialClose(fd);
    fclose(fp);
    return 0;
}

int main(){
    //setup wiringx
    if(wiringXSetup(WIRINGX_TARGET, NULL) == -1) {
        wiringXGC();
        return -1;
    }
    //define wiringx serial parameters
    struct wiringXSerial_t espUart = {115200, 8, 'n', 1, 'n'};
    struct wiringXSerial_t gpsUart = {9600, 8, 'n', 1, 'n'};

    //set up cv image capture parameters and open
    cv::VideoCapture cap;
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 320);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);
    cap.open(0);

    //define intermediary image matrix
    cv::Mat image;

    //the first two captures are all black and greyscale respectively, lets deal with those
    cap >> image;
    cap >> image;

    //various declarations
    char filename[100];
    bool running = true;
    int i = 0; //image number

    //main loop
    while(running){
        getGPS(gpsUart);
        switch(waitOnUart(espUart)) {
            case 1: //save image
                fprintf(stderr, "save requested\n");
                sprintf(filename, "/root/images/out%d.jpg", i);
                cap >> image;
                cv::imwrite(filename, image);
                fprintf(stderr, "done %d\n",  i);
                break;
            case 2: //save and transmit image
                fprintf(stderr, "transmit requested\n");
                sprintf(filename, "/root/images/out%d.jpg", i);
                cap >> image;
                cv::imwrite(filename, image);
                if(transmit(filename, espUart)!=0){
                    cap.release();
                    wiringXGC();
                    return -1;
                }
                fprintf(stderr, "done %d\n",  i);
                break;
            case 3:
                running = false;
                break;
            default: 
                break;
        }
        i++;
    }

    //wrap up
    wiringXGC();
    cap.release();
    return 0;
}