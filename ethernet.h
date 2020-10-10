//---------------------------------------------------------------------------------
struct sockaddr_in srv_address;
SOCKET server_socket=SOCKET_ERROR;			// Сокет для обмена
SOCKET http_cln_socket=SOCKET_ERROR;		// Сокет клиента для http
PHOSTENT phe;
WSADATA WSAData;
CHAR IP[16];
UINT Socket=80;
SOCKET server_socket=SOCKET_ERROR;			// Сокет для обмена
//--------------------------------------------------------------
//	Creates server sock and binds to ip address and port
//--------------------------------------------------------------
SOCKET StartWebServer()
{
 SOCKET s;
 INT rc = WSAStartup(MAKEWORD(2, 2), &WSAData);
 
 UL_INFO ((LOGID, "WSAStartup....."));
 if(rc != 0)
	{
	 UL_INFO((LOGID, "WSAStartup failed. Error: %x",WSAGetLastError ()));
	 Sleep (10000);
	 return(0);
	}
 else UL_INFO((LOGID,"[success]"));
 UL_INFO((LOGID, "create socket....."));
 s = socket(AF_INET,SOCK_STREAM,0);
 if (s==INVALID_SOCKET)
	{
	 Sleep (10000);
	 return(0);
	}
 else UL_INFO((LOGID,"[success]"));
 SOCKADDR_IN si;
 si.sin_family = AF_INET;
 si.sin_port = htons(80);		// port
 si.sin_addr.s_addr = htonl(INADDR_ANY); 
 UL_INFO((LOGID,"bind socket"));
 if (bind(s,(struct sockaddr *) &si,sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
	 UL_INFO((LOGID,"Error in bind()"));
	 closesocket(s);
	 Sleep (10000);
	 return(0);
	}
 return(s);
}
//--------------------------------------------------------------
// WaitForClientConnections()
//		Loops forever waiting for client connections. On connection
//		starts a thread to handling the http transaction
//--------------------------------------------------------------
BOOL ConnectToServer (SOCKET server_sock)
{
 CHAR strC[50];
 GetDlgItemText (hDialog,IDC_EDIT19,strC,49); 
 phe = gethostbyname(strC);
 UL_INFO((LOGID, "[https] ConnectToServer(strC), [phe=%d]",phe));
 if(phe != NULL) // Копируем адрес узла
	{
	 memcpy((CHAR FAR *)&(srv_address.sin_addr), phe->h_addr, phe->h_length);
	 srv_address.sin_family = AF_INET;
	 srv_address.sin_port = htons(80);		// port
	 UL_INFO((LOGID, "[https] IP address is:=%s",inet_ntoa(srv_address.sin_addr)));
	 //ULOGW ("hostbyname \"%s\" is found ", strC);
	}
 else srv_address.sin_addr.s_addr = inet_addr(strC);
 UL_INFO((LOGID, "[https] Establish a connection to the server socket [%d | %d | %ul]",srv_address.sin_family,srv_address.sin_port,srv_address.sin_addr.s_addr));

 if(connect(server_sock, (PSOCKADDR)&srv_address, sizeof(srv_address)) < 0)
	{
	 closesocket(server_sock);
	 UL_INFO((LOGID, "[https] Establish a connection to %s ... connect Error | reason is %d",strC,WSAGetLastError ()));
	 return FALSE;
	}
 else 
	 UL_INFO((LOGID, "Establish a connection to %s ... connect success [%d]",strC,server_socket));
 return TRUE;
}
//--------------------------------------------------------------