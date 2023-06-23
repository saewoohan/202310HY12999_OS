#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 4){
    printf(2, "Usage: ln old new\n");
    exit();
  }
  if(strcmp("-h", argv[1]) == 0){
    if(link(argv[2], argv[3]) < 0)
      printf(2, "hard link %s %s: failed\n", argv[1], argv[2]);
  }
  else if (strcmp("-s", argv[1]) == 0){
    if(symlink(argv[2], argv[3]) < 0){
      printf(2, "symbolic link %s %s: failed\n", argv[1], argv[2]);
    } 
  }
  else{
    printf(2, "link failed\n");
    exit();
  }
  exit();
}
