#include <windows.h>
#include "unilog.h"

#define LOGID logg,0
#define LOG_FNAME "unigraph.log"
#define CFG_FILE "unigraph.ini"
#define MAX_PORTS 32
#define PORTNAME_LEN 15

typedef struct _OPCcfg OPCcfg;

struct _OPCcfg {
  char ports[MAX_PORTS][PORTNAME_LEN+1];
  int speed;  
  LONG read(LPSTR path);
};
