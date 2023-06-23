#include "types.h"
#include "user.h"
#include "fcntl.h"

// Parsed command representation
#define LIST  1
#define KILL  2
#define EXECUTE  3
#define MEMLIM  4
#define EXIT  5

#define MAX_INPUT_SIZE 100

int fork1(void);

//Fork
int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1);
  return pid;
}

void
runcmd(char * arg)
{
    
    //list 명령
    if (arg[0] == 'l' && arg[1] == 'i' && arg[2] == 's' && arg[3] == 't') {
        print_all();
    }

    //kill 명령
    else if(arg[0] == 'k' && arg[1] == 'i' && arg[2] == 'l' && arg[3] == 'l'){
        //parsing
        char *temp = (char*)malloc(sizeof(char)*15);
        int index = 0;
        for(int i = 5; i<strlen(arg); i++){
            temp[index] = arg[i];
            index++;
        }
        temp[index] = '\0';
        int pid  = atoi(temp);

        //pid를 가진 process를 kill
        if(kill(pid) == 0){
            printf(1, "Success!\n");
        }
        else{
            printf(1, "Fail!\n");
        }
    }

    //execute 명령
    else if(arg[0] == 'e' && arg[1] == 'x' && arg[2] == 'e' && arg[3] == 'c' && arg[4] == 'u' && arg[5] == 't' && arg[6] == 'e'){
        //parsing
        char *temp1 = (char*)malloc(sizeof(char)*15);
        char *temp2 = (char*)malloc(sizeof(char)*15);

        int index = 8;
        int j = 0;
        int k = 0;
        
        while(1){
            if(arg[index] == 32){
                break;
            }
            temp1[j] = arg[index];
            index++;
            j++;
        }
        temp1[j] = '\0';
        index++;
        while(1){
            if(arg[index] == '\0'){
                break;
            }
            temp2[k] = arg[index];
            index++;
            k++;
        }
        temp2[k] = '\0';
        int stack = atoi(temp2);
        char *argv2[1] = {temp1,0};

        //이어서 pmanager가 실행되기 위해 fork 후 exec
        if(fork1() == 0){
            if(exec2(temp1, argv2, stack) == 0){
                printf(1, "Success!\n");
            }
            else{
                printf(1, "Fail!\n");
            }
        }
    }

    //memlim 명령
     else if(arg[0] == 'm' && arg[1] == 'e' && arg[2] == 'm' && arg[3] == 'l' && arg[4] == 'i' && arg[5] == 'm'){
        //parsing
        char *temp1 = (char*)malloc(sizeof(char)*15);
        char *temp2 = (char*)malloc(sizeof(char)*15);
        int index = 7;
        int j = 0;
        int k = 0;
            
        while(1){
            if(arg[index] == 32){
                break;
            }
            temp1[j] = arg[index];
            index++;
            j++;
        }
        temp1[j] = '\0';
        int pid = atoi(temp1);
        index++;
         while(1){
            if(arg[index] == '\0'){
                break;
            }
            temp2[k] = arg[index];
            index++;
            k++;
        }
        temp2[k] = '\0';
        int limit = atoi(temp2);

        //memory limit를 설정
        if(setmemorylimit(pid, limit) == 0){
            printf(1, "Success!\n");
        }
        else{
            printf(1, "Fail!\n");
        }
            
    }
    exit();
}

int main(int argc, char *argv[]) {
    while (1) {
        printf(1, "-> ");
        char arg[MAX_INPUT_SIZE];
        gets(arg, MAX_INPUT_SIZE);
        arg[strlen(arg)-1] = 0;

        //Exit 명령, pmanger를 종료
        if(strcmp("exit", arg) == 0){
            printf(1, "Exit pmanager\n");
            break;
        }
        //fork 후 명령을 수행
        if(fork1() == 0)
            runcmd(arg);
        wait();
    }
    exit();
}