#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]){
    //schedulerLock(atoi(argv[1]));
    for(long long i= 0 ; i<4000000000;i++){
        for(long long j= 0 ; j<4000000000;j++){
            for(long long k = 0; k<400000000; k++){
            long long x = i *  j *3.14 * k;
            }

        }
    }
    cps();

    exit();
}