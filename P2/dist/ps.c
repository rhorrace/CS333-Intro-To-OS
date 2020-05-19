#ifdef CS333_P2
#include "types.h"
#include "user.h"
#include "uproc.h"

int
main(void)
{
  struct uproc* table;
  int i;
  uint max = 72;
  int catch = 0;
  uint elapsed, decimal, seconds, seconds_decimal;
  table = malloc(sizeof(struct uproc) * max);
  catch = getprocs(max, table);
  if(catch == -1)
    printf(1, "\nError: Invalid max or NULL uproc table\n");
  else {
    printf(1, "\nPID\tName\tUID\tGID\tPPID\tElapsed\tCPU\tState\tSize");
    for(i = 0;i < catch;++i) {
      decimal = table[i].elapsed_ticks % 1000;
      elapsed = table[i].elapsed_ticks / 1000;
      seconds_decimal = table[i].CPU_total_ticks % 1000;
      seconds = table[i].CPU_total_ticks / 1000;
      printf(1, "\n%d\t%s\t%d\t%d\t%d\t%d.", table[i].pid, table[i].name, table[i].uid, table[i].gid, table[i].ppid, elapsed);
      if(decimal < 10)
        printf(1, "00");
      else if(decimal < 100)
        printf(1, "0");
      printf(1, "%d\t%d.", decimal, seconds);
      if(seconds_decimal < 10)
        printf(1, "00");
      else if(seconds_decimal < 100)
        printf(1, "0");
      printf(1, "%d\t%s\t%d", seconds_decimal, table[i].state, table[i].size);
    }
    printf(1, "\n");
  }
  free(table);
  exit();
}
#endif // CS333_P2
