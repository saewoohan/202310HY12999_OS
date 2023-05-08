#include "fcntl.h"

int main(int argc, char *argv[]){
   int pid = fork();
   if(pid < 0){
   }else if(pid > 0){
    // for(int i = 0 ; i<400000; i++){
    //     int x = i * 3.14;
    // }
    printf(1, "1\n");
    cps();
    wait();
   }
    else{
    for(int i = 0 ; i<300000000;i++){
        int x = i * 3.14;
        //printf(1, "%d\n", i);
    }
    printf(1,"2\n");
    cps();
    exit();
   }
   
   exit();
}