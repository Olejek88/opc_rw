//---------------------------------------------------------------------------------
#define _WIN32_DCOM				// Enables DCOM extensions
#define INITGUID				// Initialize OLE constants

#include <stdio.h>
#include <math.h>				// some mathematical function
#include "memobus.h"			// no comments
#include "memodrv.h"			// no comments
#include "unilog.h"				// universal utilites for creating log-files
#include <locale.h>				// set russian codepage
#include <opcda.h>				// basic function for OPC:DA
#include "lightopc.h"			// light OPC library header file
#include "serialport.h"			// function for work with Serial Port
#include "ethernet.h"			// function for work with EtherNet

#define ECL_SID "opc.rw"		// identificator of OPC server
//---------------------------------------------------------------------------------
static const loVendorInfo vendor = {0,30,1,1,"RW OPC Server" };// OPC vendor info (Major/Minor/Build/Reserv)
static void a_server_finished(void*, loService*, loClient*);	// OnServer finish his work
static int OPCstatus=OPC_STATUS_RUNNING;						// status of OPC server
loService *my_service;			// name of light OPC Service
//---------------------------------------------------------------------------------
CHAR CHARset[38]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZZ";		//	CHARacter table
CHAR programme[9];				// programm version
CHAR transtype;					// translate type "T" - text, "N" - name
INT tTotal=MEMOCOM_NUM_MAX;		// total quantity of tags

UCHAR protocol=0;				// protocol type
//---------------------------------------------------------------------------------
SerialPort port;				// com-port
UINT com_num=1;			// COM-port number
UINT speed=200;			// COM-port speed
UINT parity=0;			// Parity
UINT databits=0;		// Data bits 
//---------------------------------------------------------------------------------
INT c_beg=0,c_end=50;			// command on one request
CHAR clas_s;					// device class
CHAR code[5];					// access code
static Memo *devp;				// pointer to tag structure
static loTagId ti[MEMOCOM_NUM_MAX];			// Tag id
static loTagValue tv[MEMOCOM_NUM_MAX];		// Tag value
static CHAR *tn[MEMOCOM_NUM_MAX];			// Tag name
static OPCcfg cfg;							// new structure of cfg
//---------------------------------------------------------------------------------
VOID addCommToPoll();			// add commands to read list
UINT ScanBus();					// bus scanned programm
UINT eScanBus();				// bus scanned programm
UINT PollDevice(INT device, INT cbegin, INT cend);	// polling single device
UINT InitDriver();				// func of initialising port and creating service
UINT DestroyDriver();			// function of detroying driver and service
HRESULT WriteDevice(INT device,const unsigned cmdnum,LPSTR data);	// write tag to device
FILE *CfgFile, *DrvFile, *DrvFile2;		// pointer to .ini file
static CRITICAL_SECTION lk_values;		// protects ti[] from simultaneous access 
static INT mymain(HINSTANCE hInstance, INT argc, CHAR *argv[]);
static INT show_error(LPCTSTR msg);		// just show messagebox with error
static INT show_msg(LPCTSTR msg);		// just show messagebox with message
static VOID poll_device(VOID);			// function polling device
INT init_tags();						// Init tags
VOID CheckRegStatus ();					// Check Registry Stop/Run server status
INT RegisterTags ();					// Register tags
VOID dataToTag(INT device);				// copy data buffer to tag
CHAR* ReadParam (CHAR *SectionName,CHAR *Value);// read parametr from .ini file
BOOL CreateRegKeys (DWORD dwD);
UINT ReadRegKeys ();
//---------------------------------------------------------------------------------
BOOL WorkEnable=TRUE;
BOOL rvaltags=TRUE;						// show rVal tags
INT  tcount=0;					
CHAR argv0[FILENAME_MAX + 32];			// lenght of command line (file+path (260+32))
unilog *logg=NULL;						// new structure of unilog
CRITICAL_SECTION drv_access;
UCHAR DeviceDataBuffer[16][MEMOCOM_NUM_MAX][40];
CHAR  mBuf[MEMOCOM_NUM_MAX][8];
//---------------------------------------------------------------------------------
// {E4152A55-9034-4522-A6AE-C89B7A7FBCAD}
DEFINE_GUID(GID_rwOPCserverDll, 
0xe4152a55, 0x9034, 0x4522, 0xa6, 0xae, 0xc8, 0x9b, 0x7a, 0x7f, 0xbc, 0xad);
// {819619C0-C4D8-4683-B54E-3C34F95A0DA5}
DEFINE_GUID(GID_rwOPCserverExe, 
0x819619c0, 0xc4d8, 0x4683, 0xb5, 0x4e, 0x3c, 0x34, 0xf9, 0x5a, 0xd, 0xa5);
//---------------------------------------------------------------------------------
void timetToFileTime( time_t t, LPFILETIME pft )
{   LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;
    pft->dwLowDateTime = (DWORD) ll;
    pft->dwHighDateTime =  (unsigned long)(ll >>32);}

CHAR *absPath(CHAR *fileName)					// return abs path of file
{ static CHAR path[sizeof(argv0)]="\0";			// path - massive of comline
  CHAR *cp;
  if(*path=='\0') strcpy(path, argv0);
  if(NULL==(cp=strrchr(path,'\\'))) cp=path; else cp++;
  cp=strcpy(cp,fileName);
  return path;}

inline void init_common()		// create log-file
{ logg = unilog_Create(ECL_SID, absPath(LOG_FNAME), NULL, 0, ll_DEBUG); // level [ll_FATAL...ll_DEBUG] 
  UL_INFO((LOGID, "Start"));}
inline void cleanup_common()	// delete log-file
{ UL_INFO((LOGID, "Finish"));
  unilog_Delete(logg); logg = NULL;
  UL_INFO((LOGID, "total Finish"));}

INT show_error(LPCTSTR msg)			// just show messagebox with error
{ ::MessageBox(NULL, msg, ECL_SID, MB_ICONSTOP|MB_OK);
  return 1;}
INT show_msg(LPCTSTR msg)			// just show messagebox with message
{ ::MessageBox(NULL, msg, ECL_SID, MB_OK);
  return 1;}

inline void cleanup_all(DWORD objid)
{ // Informs OLE that a class object, previously registered is no longer available for use  
  if (FAILED(CoRevokeClassObject(objid)))  UL_WARNING((LOGID, "CoRevokeClassObject() failed..."));
  DestroyDriver();					// close port and destroy driver
  CoUninitialize();					// Closes the COM library on the current thread
  cleanup_common();					// delete log-file
  //fclose(CfgFile);					// close .ini file
}
//---------------------------------------------------------------------------------
#include "opc_main.h"	//	main server 
//---------------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShow)
{ static CHAR *argv[3] = { "dummy.exe", NULL, NULL };	// defaults arguments
  argv[1] = lpCmdLine;									// comandline - progs keys
  return mymain(hInstance, 2, argv);}

int main(int argc, CHAR *argv[])
{  return mymain(GetModuleHandle(NULL), argc, argv); }

int mymain(HINSTANCE hInstance, int argc, CHAR *argv[]) 
{
const CHAR eClsidName [] = ECL_SID;				// desription 
const CHAR eProgID [] = ECL_SID;				// name
DWORD objid;									// fully qualified path for the specified module
CHAR *cp;
objid=::GetModuleFileName(NULL, argv0, sizeof(argv0));	// function retrieves the fully qualified path for the specified module
if(objid==0 || objid+50 > sizeof(argv0)) return 0;		// not in border

init_common();									// create log-file
if(NULL==(cp = setlocale(LC_ALL, ".1251")))		// sets all categories, returning only the string cp-1251
	{ 
	UL_ERROR((LOGID, "setlocale() - Can't set 1251 code page"));	// in bad case write error in log
	cleanup_common();							// delete log-file
    return 0;
	}
cp = argv[1];		
if(cp)						// check keys of command line 
	{
    int finish = 1;			// flag of comlection
    if (strstr(cp, "/r"))	//	attempt registred server
		{
	     if (loServerRegister(&GID_rwOPCserverExe, eProgID, eClsidName, argv0, 0)) 
			{ show_error("Registration Failed");
			  UL_ERROR((LOGID, "Registration <%s> <%s> Failed", eProgID, argv0));  } 
		 else 
			{ show_msg("RW OPC Registration Ok");
			 UL_INFO((LOGID, "Registration <%s> <%s> Ok", eProgID, argv0));		}
		} 
	else 
		if (strstr(cp, "/u")) 
			{
			 if (loServerUnregister(&GID_rwOPCserverExe, eClsidName)) 
				{ show_error("UnRegistration Failed");
				 UL_ERROR((LOGID, "UnReg <%s> <%s> Failed", eClsidName, argv0)); } 
			 else 
				{ show_msg("RW OPC Server Unregistered");
				 UL_INFO((LOGID, "UnReg <%s> <%s> Ok", eClsidName, argv0));		}
			} 
		else  // only /r and /u options
			if (strstr(cp, "/?")) 
				 show_msg("Use: \nKey /r to register server.\nKey /u to unregister server.\nKey /? to show this help.");
				 else
					{
					 UL_WARNING((LOGID, "Ignore unknown option <%s>", cp));
					 finish = 0;		// nehren delat
					}
		if (finish) {      cleanup_common();      return 0;    } 
	}
if ((CfgFile = fopen(CFG_FILE, "r+")) == NULL )	
	{	
	 show_error("Error open .ini file");
	 UL_ERROR((LOGID, "Error open .ini file"));	// in bad case write error in log
	 return 0;
	}
if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) 
	{	// Initializes the COM library for use by the calling thread
     UL_ERROR((LOGID, "CoInitializeEx() failed. Exiting..."));
     cleanup_common();	// close log-file
     return 0;
	}
UL_INFO((LOGID, "CoInitializeEx() Ok...."));	// write to log
devp = new Memo();
if (InitDriver()) {		// open and set com-port
    CoUninitialize();	// Closes the COM library on the current thread
    cleanup_common();	// close log-file
    return 0;
  }
UL_INFO((LOGID, "InitDriver() Ok...."));	// write to log

if (FAILED(CoRegisterClassObject(GID_rwOPCserverExe, &my_CF, 
				   CLSCTX_LOCAL_SERVER|CLSCTX_REMOTE_SERVER|CLSCTX_INPROC_SERVER, 
				   REGCLS_MULTIPLEUSE, &objid)))
    { UL_ERROR((LOGID, "CoRegisterClassObject() failed. Exiting..."));
      cleanup_all(objid);		// close comport and unload all librares
      return 0; }
UL_INFO((LOGID, "CoRegisterClassObject() Ok...."));	// write to log

Sleep(1000);
my_CF.Release();		// avoid locking by CoRegisterClassObject() 

if (OPCstatus!=OPC_STATUS_RUNNING)	// ???? maybe Status changed and OPC not currently running??
	{	while(my_CF.in_use())      Sleep(1000);	// wait
		cleanup_all(objid);
		return 0;	}
addCommToPoll();		// check tags list and list who need
while(my_CF.in_use())	// while server created or client connected
    poll_device();		// polling devices else do nothing (and be nothing)
cleanup_all(objid);		// destroy himself
return 0;
}
//----------------------------------------------------------------------------------------------
VOID poll_device(VOID)
{
  FILETIME ft;
  INT devi, i, ecode=0;
  UINT p=0;
  for (devi=0; devi<devp->idnum; devi++)
	{
	 if (c_end>devp->cv_size) c_end=devp->cv_size;
	 i = c_beg;
	 while (1)
		{
		 UL_DEBUG((LOGID, "Driver poll <%d> (%d %d)", devp->ids[devi],c_beg,c_end));
		 //CheckRegStatus ();
		 if (WorkEnable) ecode=PollDevice(devp->ids[devi],c_beg,c_end);
		 if (ecode)
			{
			 dataToTag (devp->ids[devi]);
			 UL_DEBUG((LOGID, "Copy data to tag success"));
			 time(&devp->mtime);
			 timetToFileTime(devp->mtime, &ft);
			}
  		 else GetSystemTimeAsFileTime(&ft);
		 EnterCriticalSection(&lk_values);
		 for (int ci = c_beg; ci < c_end; ci++, i++) 
			{
			 WCHAR buf[64];
			 LCID lcid = MAKELCID(0x0409, SORT_DEFAULT); // This macro creates a locale identifier from a language identifier. Specifies how dates, times, and currencies are formatted
 	 		 MultiByteToWideChar(CP_ACP,	 // ANSI code page
									  0, // flags
						   devp->cv[ci], // points to the CHARacter string to be converted
				 strlen(devp->cv[ci])+1, // size in bytes of the string pointed to 
									buf, // Points to a buffer that receives the translated string
			  sizeof(buf)/sizeof(buf[0])); // function maps a CHARacter string to a wide-CHARacter (Unicode) string
// 	  	  UL_DEBUG((LOGID, "set tag <i,status,value>=<%d,%d,%s,%s>",i, devp->cv_status[ci], devp->cv[ci],buf));	
			 if (devp->cv_status[ci]) 
				{	
				 VARTYPE tvVt = tv[i].tvValue.vt;
				 VariantClear(&tv[i].tvValue);			  
				 switch (tvVt) 
					{
				 	  case VT_UI2:	UINT vi1;
									vi1 = *devp->cv[ci];
									vi1=atoi(devp->cv[ci]);
									V_UI2(&tv[i].tvValue) = vi1;
									break;
					  case VT_R4:	DOUBLE vr4;
									//UL_DEBUG((LOGID, "ffff:%s",devp->cv[ci]));
									setlocale(LC_ALL, "Russian");
					  				for (p=0;p<strlen(devp->cv[ci]);p++)
										if (*(devp->cv[ci]+p)=='.') 
											*(devp->cv[ci]+p)=',';
									//UL_DEBUG((LOGID, "ffff:%s",devp->cv[ci]));
									vr4=atof(devp->cv[ci]);
									V_R4(&tv[i].tvValue) = (float)vr4;
									break;
					  case VT_DATE: DATE date;
									VarDateFromStr(buf, lcid, 0, &date);
									V_DATE(&tv[i].tvValue) = date;
									break;
					  case VT_BSTR:
					  default:
								V_BSTR(&tv[i].tvValue) = SysAllocString(buf);
					}
				V_VT(&tv[i].tvValue) = tvVt;
		  	    tv[i].tvState.tsQuality = OPC_QUALITY_GOOD;
				}
				if (ecode == 0)
					 tv[i].tvState.tsQuality = OPC_QUALITY_UNCERTAIN;
				if (ecode == 2)
					 tv[i].tvState.tsQuality = OPC_QUALITY_DEVICE_FAILURE;
				tv[i].tvState.tsTime = ft;
			}
		 loCacheUpdate(my_service, tTotal, tv, 0);
		 LeaveCriticalSection(&lk_values);
		 c_beg=c_beg+50; c_end=c_end+50; 
		 if (c_end>devp->cv_size) c_end=devp->cv_size; 
		 if (c_beg>devp->cv_size) { c_end=50; c_beg=0; break;}
		}
   Sleep(100);
  }
}
//-------------------------------------------------------------------
loTrid ReadTags(const loCaller *, unsigned  count, loTagPair taglist[],
		VARIANT   values[],	WORD      qualities[],	FILETIME  stamps[],
		HRESULT   errs[],	HRESULT  *master_err,	HRESULT  *master_qual,
		const VARTYPE vtype[],	LCID lcid)
{  return loDR_STORED; }
//-------------------------------------------------------------------
int WriteTags(const loCaller *ca,
              unsigned count, loTagPair taglist[],
              VARIANT values[], HRESULT error[], HRESULT *master, LCID lcid)
{  
 unsigned ii,ci,devi; int i;
 CHAR cmdData[50];		// data to save massive
 CHAR *ppbuf = cmdData;
 VARIANT v;				// input data - variant type
// CHAR ldm;				
// struct lconv *lcp;		// Contains formatting rules for numeric values in different countries/regions
// lcp = localeconv();	// Gets detailed information on locale settings.	
// ldm = *(lcp->decimal_point);	// decimal point (i nah ona nujna?)
 VariantInit(&v);				// Init variant type
 UL_TRACE((LOGID, "WriteTags (%d) invoked", count));	
 EnterCriticalSection(&lk_values);	
 if (WorkEnable)
 for(ii = 0; ii < count; ii++) 
	{
      HRESULT hr = 0;
	  loTagId clean = 0;
      cmdData[0] = '\0';
      cmdData[MEMOCOM_DATALEN_MAX] = '\0';
      UL_TRACE((LOGID,  "WriteTags(Rt=%u Ti=%u)", taglist[ii].tpRt, taglist[ii].tpTi));	  
      i = (unsigned)taglist[ii].tpRt - 1;
      ci = i % devp->cv_size;
      devi = i / devp->cv_size;
      if (!taglist[ii].tpTi || !taglist[ii].tpRt || i >= tTotal) continue;
      VARTYPE tvVt = tv[i].tvValue.vt;
      hr = VariantChangeType(&v, &values[ii], 0, tvVt);
      if (hr == S_OK) 
		{
			switch (tvVt) 
				{	
				 case VT_UI2: _snprintf(cmdData, MEMOCOM_DATALEN_MAX, "%u",v.uiVal); break;
				 case VT_I2: _snprintf(cmdData, MEMOCOM_DATALEN_MAX, "%d", v.iVal);	 break; 
				 case VT_R4: UL_TRACE((LOGID, "Number input (%f)",v.fltVal));
							 _snprintf(cmdData, MEMOCOM_DATALEN_MAX, "%f", v.fltVal);
							 strcpy(cmdData, ppbuf);
							 break;
				 case VT_UI1:_snprintf(cmdData, MEMOCOM_DATALEN_MAX, "%c", v.bVal);	 break;
				 case VT_BSTR:
							 WideCharToMultiByte(CP_ACP,0,v.bstrVal,-1,cmdData,MEMOCOM_DATALEN_MAX,NULL, NULL);
							 break;
				 default:	 WideCharToMultiByte(CP_ACP,0,v.bstrVal,-1,cmdData,MEMOCOM_DATALEN_MAX,NULL, NULL);
				}
		 UL_TRACE((LOGID, "%!l WriteTags(Rt=%u Ti=%u %s %s)",hr, taglist[ii].tpRt, taglist[ii].tpTi, tn[i], cmdData));
 		 hr = WriteDevice(devp->ids[devi], devp->cv_cmdid[ci], cmdData);
		}
       VariantClear(&v);
	   if (S_OK != hr) 
			{
			 *master = S_FALSE;
			 error[ii] = hr;
			 UL_TRACE((LOGID, "Write failed"));
			}
	   else	UL_TRACE((LOGID, "Write success"));
       taglist[ii].tpTi = clean; 
  }
 LeaveCriticalSection(&lk_values); 
 return loDW_TOCACHE; 
}
//-------------------------------------------------------------------
VOID activation_monitor(const loCaller *ca, INT count, loTagPair *til){}
//-------------------------------------------------------------------
VOID addCommToPoll()
{
INT	max_com=0,i,j,flajok;	
sprintf(mBuf[0],"VVVV");
for (i=0;i<devp->cv_size;i++)
{	  
  if ((MemoCommU[i].getCmd!="nein")&&(!strstr(MemoCommU[i].name,"(rVal)"))) 
	 {
	 flajok=0;
	 for (j=0;j<=tcount;j++)
		 if (!strcmp (MemoCommU[i].getCmd,mBuf[j]))
			{ flajok=1; break;}
	 if (flajok==0) 
		{		 
		 sprintf(mBuf[tcount],"%s",MemoCommU[i].getCmd);
		 UL_DEBUG((LOGID, "Add command %s to poll. Total %d command added.",mBuf[tcount],tcount));
		 tcount++;
		}
	 else UL_DEBUG((LOGID, "Not add command %s to poll. position %d.",MemoCommU[i].getCmd,j));
	}
}}
//-----------------------------------------------------------------------------------
void dataToTag(int device)
{
int	max_com=0,k;
unsigned int j;	
CHAR *l;
CHAR buf[50];
CHAR *pbuf=buf; 
*pbuf = '\0';
for (int i=c_beg; i<c_end; i++)
	{
	 if (MemoCommU[i].scan_it)
	 {
	 for (j=0; j<MemoCommU[i].num; j++)
		{
		 //UL_DEBUG((LOGID, "tag: %s / nid: %d / sym: %d",MemoCommU[i].name,j+1+MemoCommU[i].start,DeviceDataBuffer[device][i][j+1+MemoCommU[i].start])); 
		 buf[j]=DeviceDataBuffer[device][i][j+1+MemoCommU[i].start];
		 //buf[j]=DeviceDataBuffer[device][i][j+1];
		}
	  buf[j] = '\0';	
	  strcpy (devp->cv[i],buf);
	 }	 
	 else
		{
	 	 for (j=0; j<2; j++)
			 buf[j]=DeviceDataBuffer[device][i][j+1+MemoCommU[i].start]; 
		 buf[j] = '\0';
		 strcpy (devp->cv[i],buf);

		 l=strstr(MemoCommU[i].name,"(rVal)");
		 if (l) // noscan and name with rVal
			{
		//	 UL_DEBUG((LOGID, "getCmd: %s / setCmd: %s / raz: %d",MemoCommU[i].getCmd,MemoCommU[i].setCmd,MemoCommU[i].num));
			 CHAR buff[MEMOCOM_MAX_ENUMERATE];
			 sprintf (buff,MemoCommU[i].name);
			 buff[l-MemoCommU[i].name-1]='\0';
			 int pos=0;
			 for (k=0;k<devp->cv_size;k++)  
				 if (!strcmp(MemoCommU[k].name,buff))
					{ pos=1; break;	}
			 if (pos)
			 {
			  CHAR key[MEMOCOM_MAX_ENUM_NUM][20];
			  CHAR value[MEMOCOM_MAX_ENUM_NUM][50];
			  CHAR *token;			 
			  pos=0;
		//	  UL_DEBUG((LOGID, "find token in %s and %s",MemoCommU[i].getCmd,MemoCommU[i].setCmd));
			  sprintf (buff,MemoCommU[i].getCmd);
			  token = strtok(buff,"|\n");
			  while(token != NULL )
				{				 
				 sprintf(key[pos],token);
				 token = strtok(NULL,"|\n");
				 pos++;
				}			 			 
 			  pos=0;
			  sprintf (buff,MemoCommU[i].setCmd);
			  token = strtok(buff,"|\n");
			  while(token != NULL)
				{				 			
				 sprintf(value[pos],token);
				 token = strtok(NULL,"|\n");
				 pos++;
				}			 			 			 
			  for (int kk=0;kk<pos;kk++)
				{
				 if (!strcmp(key[kk],devp->cv[k]))
					{
					 sprintf(buf,value[kk]);
					 break;
					}
				}
			 }
			}
		 strcpy (devp->cv[i],buf);
		 devp->cv_status[i] = TRUE;
		}
	 //UL_DEBUG((LOGID, "Tag N: %d. Copy data to tag %s. Num: %d. Start: %d.",i,devp->cv[i],MemoCommU[i].num,MemoCommU[i].start));
	}
}
//-----------------------------------------------------------------------------------
HRESULT WriteDevice(int device,const unsigned cmdnum,LPSTR data)
{
	int nump,cnt_false,cnt;
	unsigned int j,i;
	unsigned CHAR Out[45],sBuf1[40];
	const int sBuf[] = {0x01,0x30,0x31,0x2,0x57,0x31,0x30,0x30,0x30,0x3,0x50};	// short frame
	unsigned CHAR *Outt = Out,*Int = sBuf1; 
	unsigned CHAR ks=0; 
	DWORD dwStoredFlags;
	COMSTAT comstat;
	dwStoredFlags = EV_RXCHAR | EV_TXEMPTY | EV_RXFLAG;	
	UL_DEBUG((LOGID, "command send (write) %d",cmdnum));
	port.SetMask (dwStoredFlags);
	port.GetStatus (comstat);	

	for (nump=0;nump<=1;nump++)
	{	
	port.SetRTS ();	port.SetDTR ();
	for (i=0;i<=13;i++) 	Out[i] = (CHAR) sBuf[i];
	Out[1] = 48+device/10; Out[2] = 48+device%10; 	
	for (i=0;i<=6;i++)			// to lenght query max
		{
		 if (MemoCommU[cmdnum].setCmd[i]!=0)
			{
			 Out[4+i]=MemoCommU[cmdnum].setCmd[i];
			 ks=ks^Out[4+i];
			}
		 else break;
		}	
	for (j=i+4;j<i+4+MemoCommU[cmdnum].num;j++)  
		{
		 Out[j]=data[j-i-4];
		 ks=ks^Out[j];
		}	 
	Out[j]=32; ks=ks^Out[j]; j++;
	Out[j]=0x3; ks=ks^Out[j]; Out[1+j]=ks;

	for (i=0;i<=1+j;i++)
	{	
	 port.Write(Outt+i, 1);	port.WaitEvent (dwStoredFlags);
	 UL_DEBUG((LOGID, "send sym (write): %d = %d",i,Out[i]));
	}
	cnt_false=0;  Int = sBuf1;
	for (cnt=0;cnt<40;cnt++)
	{
	if (port.Read(Int+cnt, 1) == FALSE)
		{cnt_false++; if (cnt_false>0) break;}
	else cnt_false=0;
	UL_DEBUG((LOGID, "recieve sym (write): %d = %d.",cnt,sBuf1[cnt]));
	port.GetStatus (comstat);
	}
//-----------------------------------------------------------------------------------
	BOOL bcFF_OK = FALSE; BOOL bcFF_06 = FALSE; int chff=0;
	for (cnt=0;cnt<40;cnt++)
		{ 
		  if (sBuf1[cnt]==0x1)			// ответ от подчиненного???
			 if (sBuf1[cnt+1]==0x30)	// да еще и тот адрес
				if (sBuf1[cnt+2]==48+device)		// да еще и на команду идентификации!
					if ((sBuf1[cnt+4]==0x30)||(sBuf1[cnt+4]==0x31))		// все правильно
						bcFF_06 = TRUE; 
					else
						{
						 switch (sBuf1[cnt+4])
							{	
								case 50: UL_DEBUG((LOGID, "Write: address cannot be edited")); break;
								case 51: UL_DEBUG((LOGID, "Write: address does not be exist")); break;
								case 52: UL_DEBUG((LOGID, "Write: option not available for this address")); break;
								case 53: UL_DEBUG((LOGID, "Write: address not used at the moment")); break;
								case 54: UL_DEBUG((LOGID, "Write: address not allowed  using serial interface")); break;
								case 55: UL_DEBUG((LOGID, "Write: not allowed CHARacters in the parametr")); break;
								case 56: UL_DEBUG((LOGID, "Write: parametr logically incorect")); break;
								case 57: UL_DEBUG((LOGID, "Write: invalid data format")); break;
								case 58: UL_DEBUG((LOGID, "Write: invalid time format")); break;
								case 59: UL_DEBUG((LOGID, "Write: value not available in selection list")); break;
							}
						}
		}
	if (bcFF_06)
		  return S_OK;
	}
	return E_FAIL;
}
//-----------------------------------------------------------------------------------
UINT PollDevice(INT device, INT cbegin, INT cend)
{
 INT cnt=0,c0m=0, chff=0, num_bytes=0, startid=0, cnt_false=0;
 const int sBuf[] = {0x01,0x30,0x31,0x2,0x56,0x30,0x30,0x30,0x30,0x3,0x50};	// short frame
 UCHAR sBuf1[55],Out[15],DId[80],ks=0;
 UCHAR *Outt = Out,*DeviceId = DId,*Int = sBuf1;
 COMSTAT comstat;
 DWORD dwStoredFlags;
 BOOL bcFF_OK = FALSE;
 BOOL bcFF_06 = FALSE;
 dwStoredFlags = EV_RXCHAR | EV_TXEMPTY | EV_RXFLAG;	
 port.SetMask (dwStoredFlags);
 port.GetStatus (comstat);	
//-----------------------------------------------------------------------------------		
 for (c0m=cbegin;c0m<cend;c0m++)	
	{	
	 if (my_CF.in_use())
     if (MemoCommU[c0m].scan_it)
	 {	 
	  //UL_DEBUG((LOGID, "command request %s",MemoCommU[c0m].getCmd));
	  port.SetRTS (); port.SetDTR ();
	  for (INT i=0;i<=11;i++) 	Out[i] = (CHAR) sBuf[i]; // 5 первых ff + стандарт команды
	  Out[1] = 48+device/10; Out[2] = 48+device%10;
	  ks=0; // R21110 | R040000
	  for (i=0;i<=6;i++)			// to lenght query max
		{
		 if (MemoCommU[c0m].getCmd[i]!=0)
			{
			 Out[4+i]=MemoCommU[c0m].getCmd[i];
			 ks=ks^Out[4+i];
			}
		 else break;
		}
	  Out[4+i]=0x3; ks=ks^Out[4+i]; Out[5+i]=ks;
	  for (cnt=0;cnt<=5+i;cnt++) 
		{	port.Write(Outt+cnt, 1);	port.WaitEvent (dwStoredFlags);}
	    //UL_DEBUG((LOGID, "byte out %d",*(Outt+cnt)));

	  Int = sBuf1;	cnt_false=0;
	  for (cnt=0;cnt<50;cnt++) sBuf1[cnt]=0;
	  for (cnt=0;cnt<50;cnt++)
			{
			 if (port.Read(Int+cnt, 1) == FALSE)
				{ cnt_false++; cnt--; if (cnt_false>1) break;}		//!!! (4)
			 else cnt_false=0;
				port.GetStatus (comstat);
		//	 UL_DEBUG((LOGID, "byte in %d",*(Int+cnt)));
			 if (*(Int+cnt-1)==0x3) break;
			}
//-----------------------------------------------------------------------------------
	 chff=0; bcFF_06 = FALSE;
	 for (cnt=0;cnt<50;cnt++)
		{ 
		  if (sBuf1[cnt]==0x1)			// ответ от подчиненного???
			 if (sBuf1[cnt+1]==0x30)	// да еще и тот адрес
				if (sBuf1[cnt+2]==48+device)		// да еще и на команду идентификации!
					if ((sBuf1[cnt+4]==0x30)||(sBuf1[cnt+4]==0x31)||(sBuf1[cnt+4]==0x32))		// все правильно
						{ bcFF_06 = TRUE; startid=cnt+4;}
					else
						{
						 bcFF_06 = FALSE;
						 switch (sBuf1[cnt+4])
							{	//1
								case 50: if (Out[4]=='R') UL_DEBUG((LOGID, "Read: address cannot be edited")); break;			
								case 51: if (Out[4]=='R') UL_DEBUG((LOGID, "Read: address does not be exist")); break;
								case 52: if (Out[4]=='R') UL_DEBUG((LOGID, "Read: option not available for this address")); break;
								case 53: if (Out[4]=='R') UL_DEBUG((LOGID, "Read: address not used at the moment")); break;
								case 54: if (Out[4]=='R') UL_DEBUG((LOGID, "Read: address not allowed  using serial interface")); break;
								case 55: if (Out[4]=='R') UL_DEBUG((LOGID, "Read: parametr length incorrect")); break;
								case 57: if (Out[4]=='V') UL_DEBUG((LOGID, "Version: error"));
								break;
							}
						}
		  if (sBuf1[cnt]==0x3)
			{
			  num_bytes=cnt-startid;
			  break;
			}
		}	 
	 if (bcFF_06)
		{
		 UCHAR ks=0;
//		 UL_DEBUG((LOGID, "num_bytes=%d",num_bytes));
		 for (i=startid;i<startid+num_bytes+1;i++)
			{
			 //UL_DEBUG((LOGID, "M[%d]=0x%x",i,sBuf1[i]));
			 ks=ks^sBuf1[i];
			}
		 if (ks!=sBuf1[i])
			{ 
			 bcFF_06=FALSE; 
			 UL_DEBUG((LOGID, "ks error. ks:%d // need:%d",ks,sBuf1[i]));
			}
		 //else						 UL_DEBUG((LOGID, "ks ok. ks:%d // need:%d",ks,sBuf1[i]));
		 if (clas_s=='B') cnt=0; else cnt=1;
		 for (i=0;cnt<num_bytes;cnt++,i++)
			{ 
			 if (sBuf1[startid+cnt]==0xff)
				{
				 cnt++;
				 if(sBuf1[startid+cnt]==0xff)
 					 DeviceDataBuffer[device][c0m][i] = sBuf1[startid+cnt];
				 else 
					 DeviceDataBuffer[device][c0m][i] = sBuf1[startid+cnt]-0x80;
				}
			else
				DeviceDataBuffer[device][c0m][i] = sBuf1[startid+cnt];
			if (clas_s=='B' && sBuf1[startid+cnt]==0x7f) 
				{
				 DeviceDataBuffer[device][c0m][i]=(sBuf1[startid+cnt+1]&0xf)*16 + (*(sBuf1+startid+cnt+2)&0xf);
				 cnt=cnt+2;
				}				
				//UL_DEBUG((LOGID, "c0m:%d sym: %d = %d",c0m,i,DeviceDataBuffer[device][c0m][i]));
			}		
		 devp->cv_status[c0m]=TRUE;
		}
	else 
		{
		 devp->cv_status[c0m]=FALSE;
//		 UL_DEBUG((LOGID, "False status for command: %s tag:%d",MemoCommU[c0m].getCmd,c0m));
		}
	 }
	}	
	return 1;
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
UINT DestroyDriver()
{
  if (my_service)		
    {
      int ecode = loServiceDestroy(my_service);
      UL_INFO((LOGID, "%!e loServiceDestroy(%p) = ", ecode));	// destroy derver
      DeleteCriticalSection(&lk_values);						// destroy CS
      my_service = 0;		
    }
 port.Close();
 UL_INFO((LOGID, "Close COM-port"));						// write in log
 return	1;
}
//-------------------------------------------------------------------
UINT InitDriver()
{
 loDriver ld;							// structure of driver description
 LONG ecode;							// error code 
 tTotal = MEMOCOM_NUM_MAX;				// total tag quantity
 if (my_service) {
      UL_ERROR((LOGID, "Driver already initialized!"));
      return 0;
  }
 memset(&ld, 0, sizeof(ld));    
 ld.ldRefreshRate =5000;				// polling time 
 ld.ldRefreshRate_min = 4000;			// minimum polling time
 ld.ldWriteTags = WriteTags;			// pointer to function write tag
 ld.ldReadTags = ReadTags;				// pointer to function read tag
 ld.ldSubscribe = activation_monitor;	// callback of tag activity
 ld.ldFlags = loDF_IGNCASE;				// ignore case
 ld.ldBranchSep = '/';					// hierarchial branch separator
 ecode = loServiceCreate(&my_service, &ld, tTotal);		//	creating loService 
 UL_TRACE((LOGID, "%!e loServiceCreate()=", ecode));	// write to log returning code
 if (ecode) return 1;									// error to create service	
 InitializeCriticalSection(&lk_values);
 COMMTIMEOUTS timeouts;
 CHAR buf[50]; CHAR *pbuf=buf; 
 pbuf=ReadParam ("Port","Type"); 
 if (!strcmp(pbuf,"EtherNet")) protocol=1;
 if (!strcmp(pbuf,"Serial")) protocol=2;
 UL_INFO((LOGID, "Protocol %s",buf));
 if (protocol==1)
	{
	 pbuf = ReadParam ("Port","IP");
	 strcpy ("IP",buf);
	 Socket = atoi(ReadParam ("Port","Sock")); 
	 UL_INFO((LOGID, "Opening socket %d on ip %s",Socket,IP));
	 server_socket=StartWebServer();
	 if (server_socket)
		{
		 UL_INFO((LOGID, "[https] ConnectToServer()"));
		 ConnectToServer (server_socket);
		 UL_INFO((LOGID, "[https] ConnectToServer success [%d]",sck));
		}
	 else
		{
		 UL_INFO((LOGID, "[https] Error in StartWebServer()"));
		 return 0;
		}
	 if (eScanBus()) 
		{ 
		 UL_INFO((LOGID, "Total %d devices found",devp->idnum)); 
		 if (init_tags())	return 1; 
		 else				return 0;
		}
	 else		
		{ 
		 UL_ERROR((LOGID, "No devices found")); 
 		 return 1; 
		}
	}
 if (protocol==2)
	{
	 com_num = atoi(ReadParam ("Port","COM")); 
	 speed = atoi(ReadParam ("Port","Speed"));
	 strcpy (code,ReadParam ("Port","Code"));
	 if (!strcmp(code,"error")) strcpy (code,ReadParam ("Port","0000"));
	 pbuf = ReadParam ("Server","rVal tags");
	 if (!strcmp(pbuf,"On")) rvaltags = TRUE;
	 else rvaltags = FALSE;			
	 databits = atoi(ReadParam ("Port","Databits"));
	 pbuf=ReadParam ("Port","Parity");
	 if (!strcmp(pbuf,"EvenParity")) parity=0;
	 if (!strcmp(pbuf,"MarkParity")) parity=1;
	 if (!strcmp(pbuf,"NoParity")) parity=2;
	 if (!strcmp(pbuf,"OddParity")) parity=3;
	 if (!strcmp(pbuf,"SpaceParity")) parity=4;
	 UL_INFO((LOGID, "Opening port COM%d on speed %d with parity %d and databits %d",com_num,speed, parity, databits));	
	 //port.Open(com_num,speed, CSerialPort::NoParity, databits, CSerialPort::OneStopBit, CSerialPort::NoFlowControl, FALSE);
	 if (parity==0) if (!port.Open(com_num,speed, SerialPort::EvenParity, databits, SerialPort::OneStopBit, SerialPort::NoFlowControl, FALSE)) { UL_ERROR((LOGID, "Error open COM-port")); return 1;}
	 if (parity==2) if (!port.Open(com_num,speed, SerialPort::NoParity, databits, SerialPort::OneStopBit, SerialPort::NoFlowControl, FALSE)) { UL_ERROR((LOGID, "Error open COM-port")); return 1;}
	 timeouts.ReadIntervalTimeout = 50;
	 timeouts.ReadTotalTimeoutMultiplier = 0; 
	 timeouts.ReadTotalTimeoutConstant = 80;				// !!! (180)
	 timeouts.WriteTotalTimeoutMultiplier = 0; 
	 timeouts.WriteTotalTimeoutConstant = 50; 
	 port.SetTimeouts(timeouts);
	 UL_INFO((LOGID, "Set COM-port timeouts %d:%d:%d:%d:%d",timeouts.ReadIntervalTimeout,timeouts.ReadTotalTimeoutMultiplier,timeouts.ReadTotalTimeoutConstant,timeouts.WriteTotalTimeoutMultiplier,timeouts.WriteTotalTimeoutConstant));
	}
 UL_INFO((LOGID, "Scan bus"));
 if (ScanBus()) 
	{ 
	 UL_INFO((LOGID, "Total %d devices found",devp->idnum)); 
	 if (init_tags()) return 1; 
	 else			return 0;
	}
 else		
	{ UL_ERROR((LOGID, "No devices found")); 
 	  return 1; }
}
//----------------------------------------------------------------------------------------
UINT ScanBus()
{
const int sBuf[] = {0x01,0x30,0x31,0x2,0x56,0x30,0x30,0x30,0x30,0x3,0x50};	// V0000
UCHAR Out[15],sBuf1[40], *Outt=Out,*Int=sBuf1;
INT cnt,cnt_false,chff=0;
DWORD dwStoredFlags = EV_RXCHAR | EV_TXEMPTY | EV_RXFLAG;	
COMSTAT comstat;
port.SetMask (dwStoredFlags);
port.GetStatus (comstat);	
port.Read(Int, 1);
devp->idnum = 0;
port.SetRTS (); port.SetDTR ();
for (INT adr=1;adr<=MEMOCOM_ID_MAX;adr++)
	for (INT nump=0;nump<=0;nump++)
		{
		 port.SetRTS (); port.SetDTR ();
		 for (INT i=0;i<=11;i++) 	Out[i] = (CHAR) sBuf[i]; // 5 первых ff + стандарт команды
		 Out[1] = 48+adr/10; Out[2] = 48+adr%10;
		 Out[5] = code[0]; Out[6] = code[1]; Out[7] = code[2]; Out[8] = code[3];
		 UL_INFO((LOGID, "%d %d %d %d",Out[4],Out[5],Out[6],Out[7]));	// write in log
		 Out[10]=Out[4]^Out[5]^Out[6]^Out[7]^Out[8]^Out[9];
		 for (i=0;i<=10;i++) 
			{	port.Write(Outt+i, 1);	port.WaitEvent (dwStoredFlags);	}
		 for (i=0;i<=10;i++) UL_INFO((LOGID,"[%d] = 0x%x",i,Out[i]));
		 Int = sBuf1;	cnt_false=0;
		 for (cnt=0;cnt<39;cnt++)
			{
			 if (port.Read(Int+cnt, 1) == FALSE)
				{ cnt_false++; if (cnt_false>1) break;}		//!!! (4)
			 else cnt_false=0;
				port.GetStatus (comstat);
			}
		 for (i=0;i<cnt;i++) UL_INFO((LOGID,"[%d] = 0x%x",i,sBuf1[i]));
	BOOL bcFF_OK = FALSE;	BOOL bcFF_06 = FALSE;
	for (cnt=0;cnt<36;cnt++)
		{ 
		  if (sBuf1[cnt]==0x1)			// ответ от подчиненного???
			 if (sBuf1[cnt+1]==0x30)	// да еще и тот адрес
				if (sBuf1[cnt+2]==48+adr)		// да еще и на команду идентификации!
					if (sBuf1[cnt+4]==0x30)		// все правильно
						bcFF_06 = TRUE;
		}	
    //if (bcFF_06) 
	if (1)
		{	
		 devp->ids[devp->idnum] = adr; devp->idnum++;
		 for (cnt=0;cnt<7;cnt++) programme[cnt]=sBuf1[cnt+5];
		 UL_INFO((LOGID, "Device found on address %d",adr));	// write in log
		 break;
		}
	}	 
return devp->idnum;
}
//----------------------------------------------------------------------------------------
UINT eScanBus()
{
const int sBuf[] = {0x01,0x30,0x31,0x2,0x56,0x30,0x30,0x30,0x30,0x3,0x50};	// V0000
UCHAR Out[15];
CHAR  receivebuffer[500];
INT cnt,chff=0,err=0,size;
devp->idnum = 0;
for (INT adr=1;adr<=MEMOCOM_ID_MAX;adr++)
	{
	 for (INT i=0;i<=11;i++) 	Out[i] = (CHAR) sBuf[i]; // 5 первых ff + стандарт команды
	 Out[1] = 48+adr/10; Out[2] = 48+adr%10;
	 Out[5] = code[0]; Out[6] = code[1]; Out[7] = code[2]; Out[8] = code[3];
	 UL_INFO((LOGID, "%d %d %d %d",Out[4],Out[5],Out[6],Out[7]));
	 Out[10]=Out[4]^Out[5]^Out[6]^Out[7]^Out[8]^Out[9]; Out[11]=0;	 
	 err=send(server_socket,Out,strlen(Out),0);
	 UL_INFO((LOGID,"send (%s)=%d[%d] [%d]",Out,err,WSAGetLastError(),server_socket));
	 //for (i=0;i<=10;i++) UL_INFO((LOGID,"[%d] = 0x%x",i,Out[i]));
	 Sleep (2000);
	 while (size!=SOCKET_ERROR && size!=0)
		{
		 size = SocketRead(server_socket, receivebuffer, 450);
		 if (size!=SOCKET_ERROR && size!=0) UL_INFO((LOGID,"recieve (%s)",receivebuffer));
		} 
	 for (i=0;i<cnt;i++) UL_INFO((LOGID,"[%d] = 0x%x",i,receivebuffer[i]));
	 BOOL bcFF_OK = FALSE;	BOOL bcFF_06 = FALSE;
	 for (cnt=0;cnt<36;cnt++)
		{ 
		  if (receivebuffer[cnt]==0x1)			// ответ от подчиненного???
			 if (receivebuffer[cnt+1]==0x30)	// да еще и тот адрес
				if (receivebuffer[cnt+2]==48+adr)		// да еще и на команду идентификации!
					if (receivebuffer[cnt+4]==0x30)		// все правильно
						bcFF_06 = TRUE;
		}	
	if (1)
		{	
		 devp->ids[devp->idnum] = adr; devp->idnum++;
		 for (cnt=0;cnt<7;cnt++) programme[cnt]=receivebuffer[cnt+5];
		 UL_INFO((LOGID, "Device found on address %d",adr));	// write in log
		 break;
		}
	}	 
return devp->idnum;
}
//-----------------------------------------------------------------------------------
CHAR* ReadParam (CHAR *SectionName,CHAR *Value)
{
CHAR buf[150]; 
CHAR string1[50];
CHAR *pbuf=buf;
UINT s_ok=0;
sprintf(string1,"[%s]",SectionName);
rewind (CfgFile);
	  while(!feof(CfgFile))
		 if(fgets(buf,50,CfgFile)!=NULL)
			if (strstr(buf,string1))
				{ s_ok=1; break; }
if (s_ok)
	{
	 while(!feof(CfgFile))
		{
		 if(fgets(buf,100,CfgFile)!=NULL&&strstr(buf,"[")==NULL&&strstr(buf,"]")==NULL)
			{
			 for (s_ok=0;s_ok<strlen(buf)-1;s_ok++)
				 if (buf[s_ok]==';') buf[s_ok+1]='\0';
			 if (strstr(buf,Value))
				{
				 for (s_ok=0;s_ok<strlen(buf)-1;s_ok++)
					if (s_ok>strlen(Value)) buf[s_ok-strlen(Value)-1]=buf[s_ok];
						 buf[s_ok-strlen(Value)-1]='\0';
				// UL_INFO((LOGID, "Section name %s, value %s, che %s",SectionName,Value,buf));	// write in log
				 return pbuf;
				}
			}
		}	 	
 	 if (SectionName=="Port")	{ buf[0]='1'; buf[1]='\0';}
	 return pbuf;
	}
else{
	 sprintf(buf, "error");			// if something go wrong return error
	 return pbuf;
	}	
}
//------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------
INT init_tags()
{
 BOOL sett=FALSE;
 CHAR drv[FILENAME_MAX + 32],drv1[150]; 
 CHAR buf[350],pbuf1[150],pbuf2[1000],longd[1500]; 
 CHAR *pbuf=buf; CHAR* token; CHAR *ppbuf = drv;
 CHAR value[MEMOCOM_MAX_ENUM_NUM][50];
 int pos,tag_num=0,section=0;		// 1 - $MOMENTIAN, 2 - $TABLE
 CHAR treev[7][50];					//	[level][max_lenght]
 int level=0;						// current tree level
 int repeat[7]={0,0,0,0,0,0,0};		// repeat flag + quantity of repeat
 int nextstr=0;						// разрывы строк
 int num_stars=0;					// number of stars in param
 long bytes_ar=0;
//-------------------------------------------------------------------------------------------
 pbuf = ReadParam ("Server","Driver");
 if (!strcmp(pbuf,"Auto"))
	{ sprintf (drv,"UnitDrv\\%s.drv",programme);
	  UL_INFO((LOGID, "init tags %s",drv));	// in bad case write error in log 
	  if (((DrvFile = fopen(drv, "r+")) == NULL) || ((DrvFile2 = fopen(drv, "r+")) == NULL))
		{		 
		 UL_INFO((LOGID, "Error loading driver %s",drv));	// in bad case write error in log
		 show_error("Error loading driver"); return 1;	// jopa
		}	
	}
 else
	{ sprintf (drv,"UnitDrv\\%s.drv",pbuf);
	  UL_INFO((LOGID, "init tags %s",drv));	// in bad case write error in log 
	  if (((DrvFile = fopen(drv, "r+")) == NULL) || ((DrvFile2 = fopen(drv, "r+")) == NULL))
		{
		 UL_INFO((LOGID, "Error loading driver %s",drv));	// in bad case write error in log
		 show_error("Error loading driver"); return 1;	// jopa
		}	
	}
//-------------------------------------------------------------------------------------------
 pbuf = ReadParam ("Server","Settings"); 
 longd[0]='\0';
 if (!strcmp(pbuf,"On")) sett=TRUE;
 while(!feof(DrvFile))
	{
	 if(fgets(buf,350,DrvFile)!=NULL)
		{
		 int j1=0;
		 for (int j2=0;j2<=350;j2++)
			 if (buf[j2]==0x20) j1++;
			 else break;
		 strncpy(buf,j1+buf,strlen(buf)-j1); *(buf+strlen(buf)-j1-1)='\0';
		 if (strstr(buf,"$DEFINITION")) section=3;
		 if (strstr(buf,"$MOMENTAN")) section=1;
		 if (strstr(buf,"$CONFIGURATION")) section=4;
 		 if (strstr(buf,"$TABLE")) section=2;
		 if (section==3 && strstr(buf,"%TRANSFER"))
			{
			 CHAR* begin=strstr(buf,"=");
			 transtype = *(begin+1);
			 UL_INFO((LOGID, "Translate type %c",transtype));
			}
		 if (section==4 && strstr(buf,"%CLASS"))
			{
			 CHAR* begin=strstr(buf,"=");
			 clas_s = *(begin+1);
			 UL_INFO((LOGID, "Class %c",clas_s));
			}
		 if (strstr(buf,"D;")||strstr(buf,"A;")||strstr(buf,"#")||strstr(buf,";~")||strstr(buf,"P;")||strstr(buf,"F;")||strstr(buf,";@"))
			{
			 strcpy(pbuf2,buf);
			 token = strtok(buf,";\n");
			 pos=1;
			 while(token != NULL)
				{
				 sprintf(value[pos],token);
				 token = strtok(NULL,";\n"); pos++;
				}
			 value[pos][0]='@';			 
			 if (section==1)
				{
				 int qua3=atoi(value[3]);
				 for (int i=0; i<qua3;i++)
					{
					 MemoCommU[tag_num].dtype = VT_BSTR;					 
					 if (!strcmp(value[1],"A") || !strcmp (value[1],"M")) MemoCommU[tag_num].dtype = VT_R4;
					 if (!strcmp (value[1],"D")) MemoCommU[tag_num].dtype = VT_UI2;
					 int qua = atoi(value[4]);
//					 for (int j=0;j<=7; j++)
//						{
//						 UL_DEBUG((LOGID, "value[2][%d]=%d",j,value[2][j]));
//						}					 
					 sprintf (drv,"R%s",value[2]); // +R
					 if (strlen(value[2])<=4)
						drv[2]=CHARset [value[2][1]+i+qua-48];
					 else
						drv[3]=CHARset [value[2][2]+i+qua-48];
					 //sprintf (drv,"R%c%c%c%c",value[2][0],value[2][1]+i+qua,value[2][2],value[2][3]);
					 //UL_DEBUG((LOGID, "getcmd %s",drv)));
					 
					 MemoCommU[tag_num].getCmd = new CHAR[7];
					 bytes_ar=bytes_ar+7;
					 strcpy (MemoCommU[tag_num].getCmd,drv);
					 MemoCommU[tag_num].setCmd = "nein";
					 //strcpy (MemoCommU[tag_num].setCmd,"nein");
					 value[5][0]=';'; 					 
		//			 UL_DEBUG((LOGID, "ishem type %s",value[5]));
					 rewind (DrvFile2);
					 
					 sprintf (drv,"Channal %d",value[2][1]-48+i+qua);  // new!!!
					 
					 while(!feof(DrvFile2))
						{
					 	 if(fgets(buf,150,DrvFile2)!=NULL)
							if (strstr(buf,value[5]))
								{
								  CHAR* begin=strstr(pbuf2,"#");
								  CHAR* end=strstr(pbuf2,";");
								  strncpy(drv,begin+1,end-begin-1);
								  *(drv+(end-begin-1)) = '\0';
								  sprintf (drv,"%s/",drv);
								  //UL_DEBUG((LOGID, "name = %s, lenght=%d",drv,end-begin));
								  begin=strstr(pbuf1,"##");
								  end=strstr(pbuf1,";");								 
								  strncpy(drv1,begin+2,end-begin-2);
								  *(drv1+(end-begin-2)) = '\0';
								  sprintf (drv,"%s%s %d",drv,drv1,value[2][1]-48+i+qua);
								  //UL_DEBUG((LOGID, "name = %s",drv));
								  break;
								}	
						 strcpy (pbuf2,pbuf1);
						 strcpy (pbuf1,buf);
						}
					 MemoCommU[tag_num].name = new CHAR [MEMOCOM_MAX_NAMEL];
					 bytes_ar=bytes_ar+MEMOCOM_MAX_NAMEL;
					 //*(drv)=tag_num*4+210; *(drv+1)='\0';
					 strcpy (MemoCommU[tag_num].name,drv);
					 UL_DEBUG((LOGID, "tag %d | name %s | getcmd %s | setcmd %s",tag_num,MemoCommU[tag_num].name,MemoCommU[tag_num].getCmd,MemoCommU[tag_num].setCmd));
					 MemoCommU[tag_num].num = 12;	// new!!!
					 MemoCommU[tag_num].start = 0;
					 MemoCommU[tag_num].scan_it = TRUE;
					 tag_num++;
					}
				}
			if (section==2 && sett)
				{
				 CHAR* begin; CHAR* begin2;
				 pbuf1[0]='\0';
				 if ((begin=strstr(value[1],"#"))!=NULL)	// есть ли решетка?
					{	// находим первую решетку в токене1	
					 level=0; // считаем количество решеток подряд = уровень вложения
					 //UL_DEBUG((LOGID, "# found in %s",value[1]));					 
					 for (int j=0;j<20;j++)
						 if (*(begin+j)=='#') level++;
						 else if (*(begin+j)!=' ') break;
					 //UL_DEBUG((LOGID, "Q# = %d",level));
					 if ((begin2=strstr(value[1],"/"))!=NULL)
						{
						 *begin2='_'; if ((begin2=strstr(value[1],"/"))!=NULL) *begin2='_';
						}
					 strncpy(drv,begin+j,strlen(value[1])-j);
					 *(drv+strlen(value[1])-j) = '\0';
					 
					 for (j=strlen(drv)-1;j>0;j--)
						if (*(drv+j)==0x20)
							*(drv+j)=0;
						else break;
					 
					 strcpy (treev[level],drv); // текущий уровень =, заносим данные в предыдущие имена					 
					 if (value[2]>0) // если токен2 больше 0, то текущий уровень + репеатфлаг = 1 репеат\уровеньтекущий\=токен2
						{
						 repeat[level]=atoi(value[2]);
						 if (repeat[level]>0) repeat[level]=repeat[level]-1;
						}						
					 else	repeat[level]=0; //иначе репеатфлаг = 0
					 for (j=level+1;j<=6;j++) repeat[j]=0;
					 //UL_DEBUG((LOGID, "level %d / name %s / repeat[level] %d",level,treev[level],repeat[level]));
					}
				 //UL_DEBUG((LOGID, "pbuf2 %s",pbuf2));
				 if ((begin=strstr(pbuf2,"~"))!=NULL)	// рещетки нету. есть ли ~ если да то флаг следующаястрока = 1 (продолжение) предыдущий = предыдущий + текущий
					{
					 // UL_DEBUG((LOGID, "~ found do %s",longd));
					  nextstr=1;
					  *begin='\0';
					  sprintf (longd,"%s%s",longd,pbuf2);
					  UL_DEBUG((LOGID, "~ found after %s",longd));
					}
				  else
					if (nextstr==1) // иначе если следующаястрока=1,  
						{
				//		 UL_DEBUG((LOGID, "first string %s without ~",pbuf2));
						 nextstr=0;		// то следующаястрока = 0
						 sprintf (longd,"%s%s",longd,pbuf2);
						 UL_DEBUG((LOGID, "~long = %s",longd));
						 // токенизация суммы и функция анализа
			 			 token = strtok(longd,";\n");
						 pos=1;
						 while(token != NULL )
							{
							 sprintf(value[pos],token);
							 //UL_DEBUG((LOGID, "token[%d]=values %s",pos,value[pos]));
							 token = strtok(NULL,";\n"); pos++;
							}						 
						 value[pos][0]='@';
						 longd[0]='\0';	// и предыдущий = \0
						 goto hui;
						}
					else
						{
hui:					 if (strcmp(value[1],"P")==0 || strcmp(value[1],"F")==0) // если токен1 == П или Ф
							{
//							 UL_DEBUG((LOGID, "parametr found %s, level %d",value[1],level));
//		repeat 1.[0],2,[16],3,[4] level = 3
		 					 for (int j=strlen(value[3])-1;j>0;j--)
								 if (*(value[3]+j)==0x20)
									*(value[3]+j)=0;
								 else break;

							 int k1[5]={0,0,0,0,0};
							 //UL_DEBUG((LOGID, "%d(%d) %d(%d) %d(%d) %d(%d)",k1[1],repeat[1],k1[2],repeat[2],k1[3],repeat[3],k1[4],repeat[4]));
							 for (k1[1]=0;k1[1]<=repeat[1];k1[1]++)
							 for (k1[2]=0;k1[2]<=repeat[2];k1[2]++)							 
							 for (k1[3]=0;k1[3]<=repeat[3];k1[3]++)
							 for (k1[4]=0;k1[4]<=repeat[4];k1[4]++)
								{
								 //UL_DEBUG((LOGID, "%d(%d) %d(%d) %d(%d) %d(%d)",k1[1],repeat[1],k1[2],repeat[2],k1[3],repeat[3],k1[4],repeat[4]));
								 drv[0]='\0';				
								 for (int i=1;i<=level;i++)
									if (i>1) 
										{
										 if (repeat[i]>0)
											{
											 itoa(k1[i]+1,drv1,10);
											 sprintf (drv,"%s/%s %s",drv,treev[i],drv1);
											}
										 else
											sprintf (drv,"%s/%s",drv,treev[i]);
										}
									else strcpy(drv,treev[i]);
								 if ((begin=strstr(value[3],"/"))!=NULL)
									 *(begin)='_';
								 sprintf (drv,"%s/%s",drv,value[3]);
								 MemoCommU[tag_num].name = new CHAR [MEMOCOM_MAX_NAMEL];
								 bytes_ar=bytes_ar+MEMOCOM_MAX_NAMEL;
								 strcpy (MemoCommU[tag_num].name,drv);
								 for (int l=0;l<=22;l++)	
										if (value[4][l]!=' ') break;
								 MemoCommU[tag_num].start = l;								 
								 //UL_DEBUG((LOGID, "type %s",value[4]));
								 if (strstr(value[4],"*") && transtype=='N')
									{
									 MemoCommU[tag_num+1].name = new CHAR [MEMOCOM_MAX_NAMEL];
									 bytes_ar=bytes_ar+MEMOCOM_MAX_NAMEL;
									 sprintf (drv,"%s (rVal)",drv);									 
									 UL_DEBUG((LOGID, "tag %d | name %s",tag_num+1,drv));
									 strcpy (MemoCommU[tag_num+1].name,drv);
 									 MemoCommU[tag_num].scan_it = TRUE;
									 MemoCommU[tag_num].dtype = VT_BSTR;

									 num_stars = strrchr (value[4],'*')-value[4]+1;
									 MemoCommU[tag_num].num = num_stars;
									}
								 else
									{
									 MemoCommU[tag_num].scan_it = TRUE;
									 if (strstr(value[4],"$IP")) 
										{
										 MemoCommU[tag_num].dtype = VT_BSTR;
										 MemoCommU[tag_num].num = 15;
										}
									 else
										{
										 for(int l=MemoCommU[tag_num].start;l<=22;l++)
											if (value[4][l]==' '||value[4][l]==0)
												break;
										 MemoCommU[tag_num].dtype = VT_BSTR;
										 MemoCommU[tag_num].num = l-MemoCommU[tag_num].start;
										 for (l=0;l<tag_num;l++)
										   if (!strcmp(MemoCommU[l].name,MemoCommU[tag_num].name))
												{
												 MemoCommU[l].num=22;
												 MemoCommU[l].start=0;
												 UL_INFO((LOGID, "double name tags found"));
												 tag_num=l;
												}
										}
									}
								 sprintf (drv,"R%s",value[2]); // +R
								 sprintf (pbuf2,"W%s",value[2]); // +W
								 //UL_DEBUG((LOGID, "getcmd %s,setcmd %s",drv,pbuf2));
								 //UL_DEBUG((LOGID, "strlen %d, k1[2] %d, value[2][1] %d, ch %c",strlen(value[2]),k1[2],value[2][1],CHARset [value[2][1]+k1[2]-48]));
								 //CHAR CHARset[38]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZZ";
								 int ind=0;
								 if (value[2][1]>57) ind=7;								 
								 if (strlen(value[2])<=4)	// P;110;  1 // P;21Z0; 1 // P;13001; 2
									{
									 pbuf2[2]=CHARset [value[2][1]+k1[2]-48-ind];
									 if (level==3) pbuf2[3]=CHARset [value[2][2]+k1[3]-48];
									 drv[2]=CHARset [value[2][1]+k1[2]-48-ind];
									 if (level==3) drv[3]=CHARset [value[2][2]+k1[3]-48];
									}
								 else
									{
									 pbuf2[3]=CHARset [value[2][2]+k1[3]-48];
									 if (level==3) pbuf2[4]=CHARset [value[2][3]+k1[4]-48-ind];
									 drv[3]=CHARset [value[2][2]+k1[3]-48];
									 if (level==3) drv[4]=CHARset [value[2][3]+k1[4]-48-ind];
									}								
								 MemoCommU[tag_num].getCmd = new CHAR [strlen(drv)+1];
								 bytes_ar=bytes_ar+strlen(drv)+1;
								 strcpy (MemoCommU[tag_num].getCmd,drv);
								 MemoCommU[tag_num].setCmd = new CHAR [strlen(pbuf2)+1];
								 bytes_ar=bytes_ar+strlen(pbuf2)+1;
								 strcpy (MemoCommU[tag_num].setCmd,pbuf2);
								 UL_DEBUG((LOGID, "tag %d | name %s | getcmd %s | setcmd %s | nums %d | start %d",tag_num,MemoCommU[tag_num].name,MemoCommU[tag_num].getCmd,MemoCommU[tag_num].setCmd,MemoCommU[tag_num].num,MemoCommU[tag_num].start));
								 tag_num++;
							//	 UL_DEBUG((LOGID, "tags %d",tag_num));
								 if (strstr(value[4],"*") && transtype=='N')
									{
									 pbuf2[0]='\0'; drv[0]='\0';
									 //UL_DEBUG((LOGID, "form (rVal)"));
									 for (int l=5;l<=100;l++)
										{
										 if (strstr(value[l],"@"))
											 break;
										 itoa(l-5,drv1,10);
										 if (l>5)
											{
											 if (l<15 && num_stars>1) sprintf (drv,"%s|0%s",drv,drv1);
											 else sprintf (drv,"%s|%s",drv,drv1);
											 sprintf (pbuf2,"%s|%s",pbuf2,value[l]);
											}
										 else
 											{
											 if (num_stars>1) sprintf (drv,"0%s",drv1);
											 else sprintf (drv,"%s",drv1);
											 sprintf (pbuf2,"%s",value[l]);
											}
										}
									 MemoCommU[tag_num].start = 0;
 									 MemoCommU[tag_num].scan_it = FALSE;
									 MemoCommU[tag_num].dtype = VT_BSTR;
									 MemoCommU[tag_num].num = l-5;
									 //UL_DEBUG((LOGID, "getcmd %s,setcmd %s",drv,pbuf2));
									 MemoCommU[tag_num].getCmd = new CHAR [strlen(drv)+1];
									 bytes_ar=bytes_ar+strlen(drv)+1;
									 strcpy (MemoCommU[tag_num].getCmd,drv);
									 bytes_ar=bytes_ar+strlen(pbuf2)+1;
									 MemoCommU[tag_num].setCmd = new CHAR [strlen(pbuf2)+1];
									 strcpy (MemoCommU[tag_num].setCmd,pbuf2);
									 UL_DEBUG((LOGID, "tag %d | name %s | getcmd %s | setcmd %s | nums %d",tag_num,MemoCommU[tag_num].name,MemoCommU[tag_num].getCmd,MemoCommU[tag_num].setCmd,MemoCommU[tag_num].num));
									 tag_num++;
									}
								}		
							}
						}

				}
			}
	}}
 fclose(DrvFile);
 fclose(DrvFile2);
 devp->cv_size = tag_num - 1;
 UL_DEBUG((LOGID, "total memory allocated in heap %d",bytes_ar));
 RegisterTags ();
 return 0;
}
//-------------------------------------------------------------------------------------------
INT RegisterTags ()
{
  FILETIME ft;	//  64-bit value representing the number of 100-ns intervals since January 1,1601
  UINT rights=0;	// tag type (read/write)
  INT ecode,devi;
  GetSystemTimeAsFileTime(&ft);	// retrieves the current system date and time
  EnterCriticalSection(&lk_values);
  for (INT i=0; i < tTotal; i++)    
	  tn[i] = new CHAR[MEMOCOM_MAX_NAMEL];	// reserve memory for massive
  for (devi = 0, i = 0; devi < devp->idnum; devi++) 
  {
    for (int ci=0;ci<=devp->cv_size; ci++, i++) 
		{
		 int cmdid = devp->cv_cmdid[ci];
		 if (protocol==1) sprintf(tn[i], "eth %s/id%0.2d/%s",IP, devp->ids[devi], MemoCommU[cmdid].name);
		 if (protocol==2) sprintf(tn[i], "com%d/id%0.2d/%s",com_num, devp->ids[devi], MemoCommU[cmdid].name);
		 rights=0;
		 if (strcmp(MemoCommU[cmdid].getCmd,"nein")) rights = rights | OPC_READABLE;
	     if (strcmp(MemoCommU[cmdid].setCmd,"nein")) rights = rights | OPC_WRITEABLE;
		 if (strstr(MemoCommU[cmdid].name,"(rVal)")) rights = OPC_READABLE;
		 VariantInit(&tv[i].tvValue);
		 if ((rvaltags&&strstr(MemoCommU[cmdid].name,"(rVal)"))||!strstr(MemoCommU[cmdid].name,"(rVal)"))
			 {
			  WCHAR buf[MEMOCOM_MAX_NAMEL];
			  LCID lcid = MAKELCID(0x0409, SORT_DEFAULT); // This macro creates a locale identifier from a language identifier. Specifies how dates, times, and currencies are formatted
	 		  MultiByteToWideChar(CP_ACP, 0,tn[i], strlen(tn[i])+1,	buf, sizeof(buf)/sizeof(buf[0])); // function maps a CHARacter string to a wide-CHARacter (Unicode) string				
			  switch (MemoCommU[ci].dtype)
				{
				 case VT_UI2:
						V_UI2(&tv[i].tvValue) = 0;
						V_VT(&tv[i].tvValue) = VT_UI2;
						ecode = loAddRealTag_aW(my_service, &ti[i], (loRealTag)(i+1), buf, 0, rights, &tv[i].tvValue, 0, 0); break;
				 case VT_R4:
						V_R4(&tv[i].tvValue) = 0.0;
						V_VT(&tv[i].tvValue) = VT_R4;
						ecode = loAddRealTag_aW(my_service, &ti[i], (loRealTag)(i+1), buf, 0, rights, &tv[i].tvValue, 0, 0); break;
				 case VT_UI1:
						V_UI1(&tv[i].tvValue) = 0;
						V_VT(&tv[i].tvValue) = VT_UI1;
						ecode = loAddRealTag_a(my_service, &ti[i], (loRealTag)(i+1),tn[i], 0, rights, &tv[i].tvValue, -99, 99); break;
				 case VT_DATE:
						V_DATE(&tv[i].tvValue) = 0;
						V_VT(&tv[i].tvValue) = VT_DATE;
						ecode = loAddRealTag_a(my_service, &ti[i], (loRealTag)(i+1),tn[i], 0, rights, &tv[i].tvValue, 0, 0); break;
				 default:
						V_BSTR(&tv[i].tvValue) = SysAllocString(L"");
						V_VT(&tv[i].tvValue) = VT_BSTR;						
						ecode = loAddRealTag_aW(my_service, &ti[i], (loRealTag)(i+1), buf, 0, rights, &tv[i].tvValue, 0, 0);
				}
			}
		 tv[i].tvTi = ti[i];
		 tv[i].tvState.tsTime = ft;
		 tv[i].tvState.tsError = S_OK;
		 tv[i].tvState.tsQuality = OPC_QUALITY_NOT_CONNECTED;
		 UL_TRACE((LOGID, "%!e loAddRealTag(%s) = %u", ecode, tn[i], ti[i]));
		}	
  } 
  LeaveCriticalSection(&lk_values);
  if(ecode) 
  {
    UL_ERROR((LOGID, "%!e driver_init()=", ecode));
    return -1;
  }
  return 0;
}





//--------------------------------------------------------------------------------------------
BOOL CreateRegKeys (DWORD dwD)
{
HKEY hk;
DWORD dwData=dwD; // "1" - start/ "0" - stop
if (RegCreateKey(HKEY_LOCAL_MACHINE,"SOFTWARE\\RW OPC Server\\Server Run", &hk)) 
	{
	 //UL_ERROR((LOGID, "Could not create registry entry"));
	 return FALSE;
	}
if (RegSetValueEx(hk,"RW OPC stop Ack.",0,REG_DWORD,(LPBYTE) &dwData,sizeof(DWORD)))
	{
	 //UL_ERROR((LOGID, "Could not create registry entry"));
	 return FALSE;
	}
UL_INFO((LOGID, "Create/update registry key \\RW OPC stop Ack. = %d",dwData));
return TRUE;
}
//---------------------------------------------------------------------------------
UINT ReadRegKeys ()
{
HKEY hKey;
LONG lRet;
DWORD dwData;
DWORD dwBufLen = sizeof(DWORD);
if(RegOpenKeyEx(HKEY_LOCAL_MACHINE,TEXT("SOFTWARE\\RW OPC Server\\Server Run"),0,KEY_QUERY_VALUE,&hKey) != ERROR_SUCCESS) 
{	//UL_ERROR((LOGID, "Could not create registry entry"));
	return -1;}
lRet = RegQueryValueEx(hKey,TEXT("RW OPC stop"),NULL,NULL,(LPBYTE) &dwData,&dwBufLen);
//UL_INFO((LOGID, "RegQueryValueEx %d",dwData));
if(lRet != ERROR_SUCCESS) return -1;
if (dwData==0) return 0;
else return 1;
}
//---------------------------------------------------------------------------------
VOID CheckRegStatus ()
{ 
 if (ReadRegKeys()==1) // we must temporary stop server
	{ 
	  if (WorkEnable)	// but we dont stopped yet
		{
		  UL_INFO((LOGID, "Attempt stop server"));
		  port.Close();			// close port
		  WorkEnable=FALSE;		// stop server
		  Sleep (500);			// pause to answer
		  CreateRegKeys (1);	// answer
		  UL_INFO((LOGID, "Server stopped"));
		}
	}
 else
	{
	  if (!WorkEnable)	// but we dont run yet
		{
		 UL_INFO((LOGID, "Attempt run server"));
		 UL_INFO((LOGID, "Opening port COM%d on speed %d",com_num,speed));	
		 if (parity==0) if (!port.Open(com_num,speed, SerialPort::EvenParity, databits, SerialPort::OneStopBit, SerialPort::NoFlowControl, FALSE)) { UL_ERROR((LOGID, "Error open COM-port")); WorkEnable=TRUE;}
		 if (parity==2) if (!port.Open(com_num,speed, SerialPort::NoParity, databits, SerialPort::OneStopBit, SerialPort::NoFlowControl, FALSE)) { UL_ERROR((LOGID, "Error open COM-port")); WorkEnable=TRUE;}
		 Sleep (500);			// pause to answer
		 CreateRegKeys (0);	// answer
		 UL_INFO((LOGID, "Server running"));
	  }
	}
}
//---------------------------------------------------------------------------------