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

//stolen file size function
int fsize(FILE *fp){
    int prev=ftell(fp);
    fseek(fp, 0L, SEEK_END);
    int sz=ftell(fp);
    fseek(fp,prev,SEEK_SET); //go back to where we were
    return sz;
}

int waitOnUart(struct wiringXSerial_t wiringXSerial){
    char buf[1024];

    int recv_len = 0;
    int i;
    int fd;

    //open serial connection
    if ((fd = wiringXSerialOpen("/dev/ttyS1", wiringXSerial)) < 0) {
        fprintf(stderr, "Open serial device failed: %d\n", fd);
        wiringXGC();
        return -1;
    }
    wiringXSerialFlush(fd);

    //hang until data is recieved
    while(recv_len == 0){
        recv_len = wiringXSerialDataAvail(fd);
        if (recv_len > 0) {
            fprintf(stderr, "recieved!\n");
            i = 0;
            while (recv_len--)
            {
                buf[i++] = wiringXSerialGetChar(fd);
            }
            if (recv_len > 1){
                case buf[0]{
                    case 't': //transmit image instruction
                        wiringXSerialPuts(fd, "ACK");
                        return 1;
                    case 's': //shutdown program instruction
                        wiringXSerialPuts(fd, "ACK");
                        return 2;
                    default: //unrecognized instruction
                        wiringXSerialPuts(fd, "?");
                        return 0;
                }
            }
        }
    }

    //case for when esp32 wants us to save image
    wiringXSerialPuts(fd, "ACK");
    wiringXSerialClose(fd);
    return 0;
}

int transmit(char* filename, struct wiringXSerial_t wiringXSerial){
    FILE* fp;
    fp = fopen(filename, "rb");

    int fd;

    int file_size = fsize(fp);

    char buf[file_size];
    int i;
    
    //open serial connection
    if ((fd = wiringXSerialOpen("/dev/ttyS1", wiringXSerial)) < 0) {
        fprintf(stderr, "Open serial device failed: %d\n", fd);
        wiringXGC();
        return -1;
    }

    //warn recipient of incoming transmission
    wiringXSerialPuts(fd, "T");

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
    struct wiringXSerial_t wiringXSerial = {115200, 8, 'n', 1, 'n'};

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
        switch(waitOnUart(wiringXSerial)) {
            case 0: //save image
                fprintf(stderr, "save requested\n");
                sprintf(filename, "/root/images/out%d.jpg", i);
                cap >> image;
                cv::imwrite(filename, image);
                fprintf(stderr, "done %d\n",  i);
                break;
            case 1: //save and transmit image
                fprintf(stderr, "transmit requested\n");
                sprintf(filename, "/root/images/out%d.jpg", i);
                cap >> image;
                cv::imwrite(filename, image);
                if(transmit(filename, wiringXSerial)!=0){
                    cap.release();
                    wiringXGC();
                    return -1;
                }
                fprintf(stderr, "done %d\n",  i);
                break;
            case 2:
                running = false;
            default: //error state
                cap.release();
                wiringXGC();
                return -1;
        }
        i++;
    }

    //wrap up
    wiringXGC();
    cap.release();
    return 0;
}