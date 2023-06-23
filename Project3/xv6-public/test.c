#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

// 테스트 함수
void triple_indirect_test(void)
{
  char filename[] = "triple_indirect_test_file";

  // 파일 생성
  int fd = open(filename, O_RDWR | O_CREATE);
  if (fd < 0) {
    printf(2, "Failed to create file\n");
    return;
  }

  char buf[BSIZE];
  memset(buf, 'A', BSIZE);

  // 파일에 데이터 블록 할당 (triple indirect)
  int i, j, k;

  printf(2, "direct block test\n");
  for (i = 0; i < NDIRECT; i++) {
    if (write(fd, buf, BSIZE) != BSIZE) {
      printf(2, "Failed to write data block\n");
      close(fd);
      return;
    }
  }
  printf(2, "direct block test passed\n");

  // Single indirect block
  printf(2, "single indirect block test\n");
  for (i = 0; i < NINDIRECT; i++) {
    if (write(fd, buf, BSIZE) != BSIZE) {
      printf(2, "Failed to write data block\n");
      close(fd);
      return;
    }
  }
  printf(2, "single indirect block test passed\n");

  // Double indirect block
  printf(2, "double indirect block test\n");
  for (i = 0; i < NINDIRECT; i++) {
    int block_addr = 0;

    if (write(fd, &block_addr, sizeof(int)) != sizeof(int)) {
      printf(2, "Failed to write double indirect block address\n");
      close(fd);
      return;
    }
    for (j = 0; j < NINDIRECT; j++) {
      if (write(fd, buf, BSIZE) != BSIZE) {
        printf(2, "Failed to write data block\n");
        close(fd);
        return;
      }
    }
  }

  printf(2, "double indirect block passed\n");

  // Triple indirect block
  printf(2, "triple indirect block test\n");
  for (i = 0; i < NINDIRECT; i++) {
    int block_addr = 0;

    if (write(fd, &block_addr, sizeof(int)) != sizeof(int)) {
      printf(2, "Failed to write triple indirect block address\n");
      close(fd);
      return;
    }

    for (j = 0; j < NINDIRECT; j++) {
      int indirect_block_addr = 0;

      if (write(fd, &indirect_block_addr, sizeof(int)) != sizeof(int)) {
        printf(2, "Failed to write indirect block address\n");
        close(fd);
        return;
      }

      for (k = 0; k < 10; k++) {
        if (write(fd, buf, BSIZE) != BSIZE) {
          printf(2, "Failed to write data block\n");
          close(fd);
          return;
        }
      }
    }
  }
  printf(2, "triple indirect block test passed\n");

  close(fd);

  printf(1, "Triple indirect test successful\n");
}

int main(void)
{
  triple_indirect_test();

  exit();
}
