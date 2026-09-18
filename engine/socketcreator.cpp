//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:  Utility class to help in socket creation. Works for clients + servers
//
//===========================================================================//


#if defined(_WIN32)
#if !defined(_X360)
#include <winsock.h>
#endif
#undef SetPort // winsock screws with the SetPort string... *sigh*
#define socklen_t int
#define MSG_NOSIGNAL 0
#elif POSIX
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <sys/ioctl.h>
#define closesocket close
#define WSAGetLastError() errno
#define ioctlsocket ioctl
#ifdef OSX
#define MSG_NOSIGNAL 0
#endif
#endif
#include <tier0/dbg.h>
#include "socketcreator.h"
#include "server.h"

#if defined( _X360 )
#include "xbox/xbox_win32stubs.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

bool SocketWouldBlock()
{
#ifdef _WIN32
	return (WSAGetLastError() == WSAEWOULDBLOCK);
#elif POSIX
	return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS);
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CSocketCreator::CSocketCreator( ISocketCreatorListener *pListener )
{
	m_hListenSocket = -1;
	m_pListener = pListener;
}


//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CSocketCreator::~CSocketCreator()
{
	Disconnect();
}

//-----------------------------------------------------------------------------
// Purpose: returns true if the listening socket is created and listening
//-----------------------------------------------------------------------------
bool CSocketCreator::IsListening() const
{
	return m_hListenSocket != -1;
}

//-----------------------------------------------------------------------------
// Purpose: Bind to a TCP port and accept incoming connections
//-----------------------------------------------------------------------------
bool CSocketCreator::CreateListenSocket( const netadr_t &netAdr )
{
	CloseListenSocket();

	m_ListenAddress = netAdr;

	// Create the socket in the family matching the requested address. An NA_IP
	// address gets AF_INET; NA_IP6 gets AF_INET6. For IPv4 we keep the legacy
	// PF_INET call so older toolchains behave identically.
	int nFamily = netAdr.IsIPv6() ? AF_INET6 : AF_INET;
	m_hListenSocket = socket ( nFamily, SOCK_STREAM, IPPROTO_TCP );
	if ( m_hListenSocket == -1 )
	{
		Warning( "Socket unable to create socket (%s)\n", NET_ErrorString( WSAGetLastError() ) );
		return false;
	}

#if defined(POSIX)
	if ( nFamily == AF_INET6 )
	{
		int v6only = 0;
		setsockopt( m_hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&v6only, sizeof(v6only) );
	}
#endif

	if ( !ConfigureSocket( m_hListenSocket ) )
	{
		CloseListenSocket();
		return false;
	}

	// sockaddr_storage is large enough for both sockaddr_in and sockaddr_in6.
	struct sockaddr_storage s;
	Q_memset( &s, 0, sizeof(s) );
	m_ListenAddress.ToSockadr( (struct sockaddr *)&s );
	socklen_t addrlen = ( nFamily == AF_INET6 ) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	int ret = bind( m_hListenSocket, (struct sockaddr *)&s, addrlen );
	if ( ret == -1 )
	{
		Warning( "Socket bind failed (%s)\n", NET_ErrorString( WSAGetLastError() ) );
		CloseListenSocket();
		return false;
	}

	ret = listen( m_hListenSocket, SOCKET_TCP_MAX_ACCEPTS );
	if ( ret == -1 )
	{
		Warning( "Socket listen failed (%s)\n", NET_ErrorString( WSAGetLastError() ) );
		CloseListenSocket();
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Configures a socket for use
//-----------------------------------------------------------------------------
bool CSocketCreator::ConfigureSocket( int sock )
{
	// disable NAGLE (rcon cmds are small in size)
	int nodelay = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay)); 

	nodelay = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&nodelay, sizeof(nodelay));

	int opt = 1, ret; 
	ret = ioctlsocket( sock, FIONBIO, (unsigned long*)&opt ); // non-blocking
	if ( ret == -1 )
	{
		Warning( "Socket accept ioctl(FIONBIO) failed (%i)\n", WSAGetLastError() );
		return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Handle a new connection
//-----------------------------------------------------------------------------
void CSocketCreator::ProcessAccept()
{
	int newSocket;
	// Must be sockaddr_storage to fit IPv6 peer addresses (28 bytes).
	sockaddr_storage sa;
	int nLengthAddr = sizeof(sa);

	newSocket = accept( m_hListenSocket, (sockaddr *)&sa, (socklen_t *)&nLengthAddr );
	if ( newSocket == -1 )
	{
		if ( !SocketWouldBlock()
#ifdef POSIX
			&& errno != EINTR 
#endif
		 )
		{
			Warning ("Socket ProcessAccept Error: %s\n", NET_ErrorString( WSAGetLastError() ) );
		}
		return;
	}

	if ( !ConfigureSocket( newSocket ) )
	{
		closesocket( newSocket );
		return; 
	}

	netadr_t adr;
	adr.SetFromSockadr( (sockaddr *)&sa );
	if ( m_pListener && !m_pListener->ShouldAcceptSocket( newSocket, adr ) )
	{
		closesocket( newSocket );
		return;
	}

	// new connection TCP request, put in accepted queue
	int nIndex = m_hAcceptedSockets.AddToTail();
	AcceptedSocket_t *pNewEntry = &m_hAcceptedSockets[nIndex];
	pNewEntry->m_hSocket = newSocket;
	pNewEntry->m_Address = adr;
	pNewEntry->m_pData = NULL;

	void* pData = NULL;
	if ( m_pListener )
	{
		m_pListener->OnSocketAccepted( newSocket, adr, &pData );
	}
	pNewEntry->m_pData = pData;
}


//-----------------------------------------------------------------------------
// Purpose: connect to the remote server
//-----------------------------------------------------------------------------
int CSocketCreator::ConnectSocket( const netadr_t &netAdr, bool bSingleSocket )
{
	if ( bSingleSocket )
	{
		CloseAllAcceptedSockets();
	}

	int nFamily = netAdr.IsIPv6() ? AF_INET6 : AF_INET;
	SocketHandle_t hSocket = socket( nFamily, SOCK_STREAM, IPPROTO_TCP );
	if ( hSocket == -1 )
	{
		Warning( "Unable to create socket (%s)\n", NET_ErrorString( WSAGetLastError() ) );
		return -1;
	}

#if defined(POSIX)
	if ( nFamily == AF_INET6 )
	{
		int v6only = 0;
		setsockopt( hSocket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&v6only, sizeof(v6only) );
	}
#endif

	int opt = 1, ret;
	ret = ioctlsocket( hSocket, FIONBIO, (unsigned long*)&opt ); // non-blocking
	if ( ret == -1 )
	{
		Warning( "Socket ioctl(FIONBIO) failed (%s)\n", NET_ErrorString( WSAGetLastError() ) );
		closesocket( hSocket );
		return -1;																	   
	}

	// disable NAGLE (rcon cmds are small in size)
	int nodelay = 1;
	setsockopt( hSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay) ); 

	struct sockaddr_storage s;
	Q_memset( &s, 0, sizeof(s) );
	netAdr.ToSockadr( (struct sockaddr *)&s );
	socklen_t addrlen = ( nFamily == AF_INET6 ) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

	ret = connect( hSocket, (struct sockaddr *)&s, addrlen);
	if ( ret == -1 )
	{
		if ( !SocketWouldBlock() )
		{	
			Warning( "Socket connection failed (%s)\n", NET_ErrorString( WSAGetLastError() ) );
			closesocket( hSocket );
			return -1;
		}

		fd_set writefds;
		struct timeval tv;
		tv.tv_usec = 0;
		tv.tv_sec = 1;
		FD_ZERO( &writefds );
		FD_SET( static_cast<u_int>( hSocket ), &writefds );
		if ( select ( hSocket + 1, NULL, &writefds, NULL, &tv ) < 1 ) // block for at most 1 second
		{
			closesocket( hSocket );		// took too long to connect to, give up
			return -1;
		}
	}

	if ( m_pListener && !m_pListener->ShouldAcceptSocket( hSocket, netAdr ) )
	{
		closesocket( hSocket );
		return -1;
	}

	// new connection TCP request, put in accepted queue
	void *pData = NULL;
	int nIndex = m_hAcceptedSockets.AddToTail();
	AcceptedSocket_t *pNewEntry = &m_hAcceptedSockets[nIndex];
	pNewEntry->m_hSocket = hSocket;
	pNewEntry->m_Address = netAdr;
	pNewEntry->m_pData = NULL;

	if ( m_pListener )
	{
		m_pListener->OnSocketAccepted( hSocket, netAdr, &pData );
	}

	pNewEntry->m_pData = pData;
	return nIndex;
}


//-----------------------------------------------------------------------------
// Purpose: close an open rcon connection
//-----------------------------------------------------------------------------
void CSocketCreator::CloseListenSocket()
{
	if ( m_hListenSocket != -1 )
	{
		closesocket( m_hListenSocket );
		m_hListenSocket = -1;
	}
}

void CSocketCreator::CloseAcceptedSocket( int nIndex )
{
	if ( nIndex >= m_hAcceptedSockets.Count() )
		return;

	AcceptedSocket_t& connected = m_hAcceptedSockets[nIndex];
	if ( m_pListener )
	{
		m_pListener->OnSocketClosed( connected.m_hSocket, connected.m_Address, connected.m_pData );
	}
	closesocket( connected.m_hSocket );
	m_hAcceptedSockets.Remove( nIndex );
}

void CSocketCreator::CloseAllAcceptedSockets()
{
	int nCount = m_hAcceptedSockets.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		AcceptedSocket_t& connected = m_hAcceptedSockets[i];
		if ( m_pListener )
		{
			m_pListener->OnSocketClosed( connected.m_hSocket, connected.m_Address, connected.m_pData );
		}
		closesocket( connected.m_hSocket );
	}
	m_hAcceptedSockets.RemoveAll();
}


void CSocketCreator::Disconnect()
{
	CloseListenSocket();
	CloseAllAcceptedSockets();
}


//-----------------------------------------------------------------------------
// Purpose: accept new connections and walk open sockets and handle any incoming data
//-----------------------------------------------------------------------------
void CSocketCreator::RunFrame()
{
	if ( IsListening() )
	{
		ProcessAccept(); // handle any new connection requests
	}
}


//-----------------------------------------------------------------------------
// Returns socket info
//-----------------------------------------------------------------------------
int CSocketCreator::GetAcceptedSocketCount() const
{
	return m_hAcceptedSockets.Count();
}

SocketHandle_t CSocketCreator::GetAcceptedSocketHandle( int nIndex ) const
{
	return m_hAcceptedSockets[nIndex].m_hSocket;
}

const netadr_t& CSocketCreator::GetAcceptedSocketAddress( int nIndex ) const
{
	return m_hAcceptedSockets[nIndex].m_Address;
}

void* CSocketCreator::GetAcceptedSocketData( int nIndex )
{
	return m_hAcceptedSockets[nIndex].m_pData;
}


