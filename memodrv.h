#include <opcda.h>              
#include <time.h>

#define MEMOCOM_DATALEN_MAX 50
#define MEMOCOM_ID_MAX	2
#define MEMOCOM_NUM_MAX 5000		// command quant
#define MEMOCOM_MAX_ENUMERATE 1100  	// max lenght of parametr
#define MEMOCOM_MAX_ENUM_NUM 200
#define MEMOCOM_MAX_NAMEL 100       	// 

typedef struct _MemoCom MemoCom;
typedef struct _Memo Memo;

struct _MemoCom {
  char *name;
  char *getCmd;
  char *setCmd;
  VARTYPE dtype;                                                  
  unsigned int start;
  unsigned int num;
  BOOL scan_it;
};
MemoCom MemoCommU[MEMOCOM_NUM_MAX];

struct _Memo {
  time_t mtime; 	// measurement time
  int cv_size;          // size (quantity scanned commands) 
  LPSTR *cv;		// pointer to value
  int *cv_cmdid;        // command identitificator
  BOOL *cv_status;	// status
  int ids[MEMOCOM_ID_MAX+1];
  int idnum;

_Memo(): mtime(0), idnum(0)
  {
    int i;    
    cv_size = MEMOCOM_NUM_MAX; // maximum

    cv = new LPSTR[MEMOCOM_NUM_MAX];	// ???
    cv_status = new BOOL[MEMOCOM_NUM_MAX]; // massive status
    cv_cmdid = new int[MEMOCOM_NUM_MAX];   // massive id
    int cv_i;				
    for (i = 0, cv_i = 0; i<MEMOCOM_NUM_MAX; i++) 
	{
	 cv[cv_i] = new char[MEMOCOM_DATALEN_MAX+1];	// init tag	
	 cv_status[cv_i] = FALSE;			// init status
	 cv_cmdid[cv_i] = i;							// init id
	 cv_i++;
    	}
  }

~_Memo()						// destructor
  {
    int i;
    for (i =0; i < cv_size; i++)
      delete[] cv[i];           // free memory

    delete[] cv_status;
    delete[] cv_cmdid;
    delete[] cv;
  }
};
