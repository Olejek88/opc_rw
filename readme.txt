Universal RW OPC Server v0.11.4

overview:
---------
RW means ReadWin. Server work on symbol protocol. Server used specific R/W text drivers,
for forming OPC tag list. Drivers can choose to load automatically,on answer 
from device, or can be set manually in .ini file. Tags divide on settings tags and
measured value tags. Settings tags also may switch off in .ini file.
All tags are string type, because driver not give any information about tags.
Server based on lightopc v0.88

installation notes:
-------------------
ReadWin drivers usually may be found after ReadWin installation in path/ReadWin32/UnitDrv folder.
To use this drivers they must be copy to: "directory when you installed Windows/System32/UnitDrv".
Some drivers installed automatically with server.
If parametr Driver in .ini file set to "Auto" server attempt detect driver for device automatically,
and if load failed you must manually set this parametr to name dirver without extension.
After install software you must register server with key /r. You also may unregistered server with key /u.
For example opc.exe /r or opc.exe /u.
Show help key /?.

version history:
----------------
v0.11 build 4
= change directory dirvers to windows/system32

v0.11 build 3
released