#ifdef CS333_P2
#include "types.h"
#include "user.h"

int
main(int argc, char* argv[])
{
  int t1 = 0, t2 = 0, elapsed = 0, decimal = 0, pid = 0;
  if(argc < 2)
    printf(1, "(null) ran in 0.000 seconds\n");
  else {
    ++argv;
    t1 = uptime();
    pid = fork();
    if(pid < 0) {
      printf(1, "Ran in 0.000 seconds\n");
      exit();
    }
    else if(pid == 0) {
      exec(argv[0], argv);
      printf(1, "Error: No such command\n");
    }
    else {
      wait();
      t2 = uptime();
      decimal = (t2 - t1) % 1000;
      elapsed = (t2 - t1) / 1000;
      printf(1, "%s ran in %d.", argv[0], elapsed);
      if(decimal < 10)
        printf(1, "00");
      else if(decimal < 100)
        printf(1, "0");
      printf(1, "%d seconds\n", decimal);
    }
  }
  exit();
}
#endif // CS333_P2
