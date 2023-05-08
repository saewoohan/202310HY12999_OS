#include "types.h"
#include "stat.h"
#include "user.h"

#define CHILD 5
#define LOOP 100000

int me;
int create_child(void){
  for(int i =0  ; i<CHILD; i++){
    int pid = fork();
    if(pid == 0){
      me = i;
      sleep(10);
      return 0;
    }
  }
  return 1;
}

void exit_child(int parent) {
	if (parent)
		while (wait() != -1); // wait for all child processes to finish
	exit();
}

int main()
{
  //just scheduling

  int p;
  //Test 1 

  // child 프로세스를 LOOP번 진행
  // p = create_child();

	// if (!p) {
  //   int pid = getpid();
	// 	int cnt[3] = {0, };
	// 	for (int i = 0; i < LOOP; i++) {
  //       cnt[getLevel()]++;
	// 	}
	// 	printf(1, "process %d: L0=%d, L1=%d, L2=%d\n", pid, cnt[0], cnt[1],cnt[2]);
	// }
	// exit_child(p);


	//Test 2

  //1개의 child 프로세스가 yield() 시스템 콜을 계속 진행
	// p = create_child();

	// if (!p) {
	// 	int pid = getpid();
	// 	int cnt[3] = {0, };
	// 	for (int i = 0; i < LOOP; i++) {
  //     //yield();
	// 		cnt[getLevel()]++;
  //   }
	// 	printf(1, "process %d: L0=%d, L1=%d, L2=%d\n", pid, cnt[0], cnt[1],cnt[2]);
	// }

	// exit_child(p);

  // Test 3

  //1개의 child 프로세스가 schedulerLock 시스템 콜을 호출
  p = create_child();

  if(!p){
    int pid = getpid();
    int cnt[3] = {0, };

    for(int i= 0 ; i<LOOP; i++){
      if(me == CHILD - 3 && i == 10000){
        schedulerLock(2019067429);
      }
      cnt[getLevel()]++;
    }
    printf(1, "process %d : L0=%d, L1=%d, L2=%d\n", pid, cnt[0], cnt[1], cnt[2]);

  }

  exit_child(p);

  //Test 4

  //1개의 child 프로세스의 priority를 setPriority를 통해 우선순위가 계속 높여줌
  // p = create_child();

	// if (!p) {
	// 	int pid = getpid();
	// 	int cnt[3] = {0, };
	// 	for (int i = 0; i < LOOP; i++) {
	// 		cnt[getLevel()]++;
	// 		if(me == CHILD-3){
  //       setPriority(pid, 0);
  //     }
	// 	}
	// 	printf(1, "process %d: L0=%d, L1=%d, L2=%d\n", pid, cnt[0], cnt[1], cnt[2]);
	// }
	
	// exit_child(p);
}
