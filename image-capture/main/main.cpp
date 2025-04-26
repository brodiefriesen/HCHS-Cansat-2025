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

int waitOnUart(struct wiringXSerial_t espUart){
    char buf[1024];

    int recv_len = 0;
    int i;
    int fd;

    //open serial connection
    if ((fd = wiringXSerialOpen("/dev/ttyS1", espUart)) < 0) {
        fprintf(stderr, "Open serial device failed: 1\n");
        wiringXGC();
        return -1;
    }

    wiringXSerialPuts(fd, "balls");

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
                return 3;
            } else{
                //just save
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
    if ((fd = wiringXSerialOpen("/dev/ttyS1", espUart)) < 0) {
        fprintf(stderr, "Open serial device failed: 1");
        wiringXGC();
        return -1;
    }

    fprintf(stderr, "filesize: %d\n", file_size);

    //copy image to serial buffer
    wiringXSerialPrintf(fd, "I");
    fread(buf, 1, file_size, fp);
    write(fd, buf, file_size);

    wiringXSerialClose(fd);
    fclose(fp);
    return 0;
}

int main(){
    //setup wiringx
    int DUO_GPIO = 0;
    if(wiringXSetup((char*) WIRINGX_TARGET, NULL) == -1) { //char pointer cast so compiler doesnt give us annoying warnings :(
        wiringXGC();
        return -1;
    }
    //define wiringx serial parameters
    //struct wiringXSerial_t espUart = {115200, 8, 'n', 1, 'n'};
    //set up cv image capture parameters and open
    cv::VideoCapture cap;
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
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

    pinMode(DUO_GPIO, PINMODE_INPUT);

    //main loop
    while(running){
        int in = digitalRead(DUO_GPIO);
        if(in==0){
            sprintf(filename, "/root/images/out%d.jpg", i);
            cap >> image;
            cv::imwrite(filename, image);
            fprintf(stderr, "done %d\n",  i);
            /*
            int ret = waitOnUart(espUart);
            if(ret==2){
                fprintf(stderr, "transmit requested\n");
                
                if(transmit(filename, espUart)!=0){
                    cap.release();
                    wiringXGC();
                    return -1;
                }

            }else if(ret==3){
                running = false;
            }
            */

            sleep(5);
            i++;
        }
    }

    //wrap up
    wiringXGC();
    cap.release();
    return 0;
}