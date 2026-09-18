//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// NetAdr.cpp: implementation of the CNetAdr class.
//
//===========================================================================//
#if defined( _WIN32 ) && !defined( _X360 )
#include <windows.h>
#endif

#include "tier0/dbg.h"
#include "netadr.h"
#include "tier1/strtools.h"

#if defined( _WIN32 ) && !defined( _X360 )
#define WIN32_LEAN_AND_MEAN
#include <winsock.h>
typedef int socklen_t;
#elif !defined( _X360 )
#include <netinet/in.h> // ntohs()
#include <netdb.h>		// getaddrinfo() / gethostbyname()
#include <sys/socket.h>	// getsockname()
#include <arpa/inet.h>	// inet_pton()
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

bool netadr_t::CompareAdr (const netadr_t &a, bool onlyBase) const
{
	if ( a.type != type )
		return false;

	if ( type == NA_LOOPBACK )
		return true;

	if ( type == NA_BROADCAST )
		return true;

	if ( type == NA_IP )
	{
		if ( !onlyBase && (port != a.port) )
			return false;

		if ( a.ip[0] == ip[0] && a.ip[1] == ip[1] && a.ip[2] == ip[2] && a.ip[3] == ip[3] )
			return true;
	}

	if ( type == NA_IP6 )
	{
		if ( !onlyBase && (port != a.port) )
			return false;

		if ( Q_memcmp( a.ip6, ip6, sizeof(ip6) ) == 0 )
			return true;
	}

	return false;
}

bool netadr_t::CompareClassBAdr (const netadr_t &a) const
{
	if ( a.type != type )
		return false;

	if ( type == NA_LOOPBACK )
		return true;

	if ( type == NA_IP )
	{
		if (a.ip[0] == ip[0] && a.ip[1] == ip[1] )
			return true;
	}

	if ( type == NA_IP6 )
	{
		// Treat the IPv6 /64 as the rough equivalent of an IPv4 class-B network:
		// two peers sharing the same /64 are "on the same local network".
		if ( Q_memcmp( a.ip6, ip6, 8 ) == 0 )
			return true;
	}

	return false;
}

bool netadr_t::CompareClassCAdr (const netadr_t &a) const
{
	if ( a.type != type )
		return false;

	if ( type == NA_LOOPBACK )
		return true;

	if ( type == NA_IP )
	{
		if (a.ip[0] == ip[0] && a.ip[1] == ip[1] && a.ip[2] == ip[2] )
			return true;
	}

	if ( type == NA_IP6 )
	{
		// Class-C equivalent: compare the first 13 bytes (a /104), i.e. everything
		// except the low 24 bits of the interface identifier.
		if ( Q_memcmp( a.ip6, ip6, 13 ) == 0 )
			return true;
	}

	return false;
}
// reserved addresses are not routeable, so they can all be used in a LAN game
bool netadr_t::IsReservedAdr () const
{
	if ( type == NA_LOOPBACK )
		return true;

	if ( type == NA_IP )
	{
		if ( (ip[0] == 10) ||									// 10.x.x.x is reserved
			 (ip[0] == 127) ||									// 127.x.x.x 
			 (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||	// 172.16.x.x  - 172.31.x.x 
			 (ip[0] == 192 && ip[1] >= 168) ) 					// 192.168.x.x
			return true;
	}

	if ( type == NA_IP6 )
	{
		// ::1 loopback
		if ( ip6[0] == 0 && ip6[1] == 0 && ip6[2] == 0 && ip6[3] == 0 &&
			 ip6[4] == 0 && ip6[5] == 0 && ip6[6] == 0 && ip6[7] == 0 &&
			 ip6[8] == 0 && ip6[9] == 0 && ip6[10] == 0 && ip6[11] == 0 &&
			 ip6[12] == 0 && ip6[13] == 0 && ip6[14] == 0 && ip6[15] == 1 )
			return true;

		// fc00::/7 Unique Local Addresses (ULA)
		if ( (ip6[0] & 0xFE) == 0xFC )
			return true;

		// fe80::/10 Link-Local
		if ( ip6[0] == 0xFE && (ip6[1] & 0xC0) == 0x80 )
			return true;
	}

	return false;
}

const char * netadr_t::ToString( bool onlyBase ) const
{
	// Select a static buffer
	static	char	s[4][64];
	static int slot = 0;
	int useSlot = ( slot++ ) % 4;

	// Render into it
	ToString( s[useSlot], sizeof(s[0]), onlyBase );

	// Pray the caller uses it before it gets clobbered
	return s[useSlot];
}

void netadr_t::ToString( char *pchBuffer, uint32 unBufferSize, bool onlyBase ) const
{

	if (type == NA_LOOPBACK)
	{
		V_strncpy( pchBuffer, "loopback", unBufferSize );
	}
	else if (type == NA_BROADCAST)
	{
		V_strncpy( pchBuffer, "broadcast", unBufferSize );
	}
	else if (type == NA_IP)
	{
		if ( onlyBase )
		{
			V_snprintf( pchBuffer, unBufferSize, "%i.%i.%i.%i", ip[0], ip[1], ip[2], ip[3]);
		}
		else
		{
			V_snprintf( pchBuffer, unBufferSize, "%i.%i.%i.%i:%i", ip[0], ip[1], ip[2], ip[3], ntohs(port));
		}
	}
	else if (type == NA_IP6)
	{
		// Render IPv6 in bracketed form so the :port separator is unambiguous.
		char addr[40];
#if defined(POSIX)
		// inet_ntop gives the canonical compressed form. POSIX-only because the
		// NA_IP6 type is only ever produced via the POSIX socket path.
		if ( inet_ntop( AF_INET6, ip6, addr, sizeof(addr) ) == NULL )
			V_strncpy( addr, "?:invalid-v6", sizeof(addr) );
#else
		// Fallback: 8 hex groups, no zero-run compression.
		V_snprintf( addr, sizeof(addr), "%x:%x:%x:%x:%x:%x:%x:%x",
			( ip6[0]<<8 )|ip6[1], ( ip6[2]<<8 )|ip6[3],
			( ip6[4]<<8 )|ip6[5], ( ip6[6]<<8 )|ip6[7],
			( ip6[8]<<8 )|ip6[9], ( ip6[10]<<8 )|ip6[11],
			( ip6[12]<<8 )|ip6[13], ( ip6[14]<<8 )|ip6[15] );
#endif
		if ( onlyBase )
		{
			V_snprintf( pchBuffer, unBufferSize, "[%s]", addr );
		}
		else
		{
			V_snprintf( pchBuffer, unBufferSize, "[%s]:%i", addr, ntohs(port) );
		}
	}
	else
	{
		V_strncpy( pchBuffer, "unknown", unBufferSize );
	}
}

bool netadr_t::IsLocalhost() const
{
	// are we 127.0.0.1 ?
	if ( type == NA_IP )
		return (ip[0] == 127) && (ip[1] == 0) && (ip[2] == 0) && (ip[3] == 1);

	// are we ::1 ?
	if ( type == NA_IP6 )
		return	ip6[0] == 0 && ip6[1] == 0 && ip6[2] == 0 && ip6[3] == 0 &&
				ip6[4] == 0 && ip6[5] == 0 && ip6[6] == 0 && ip6[7] == 0 &&
				ip6[8] == 0 && ip6[9] == 0 && ip6[10] == 0 && ip6[11] == 0 &&
				ip6[12] == 0 && ip6[13] == 0 && ip6[14] == 0 && ip6[15] == 1;

	return false;
}

bool netadr_t::IsLoopback() const
{
	// are we useding engine loopback buffers
	return type == NA_LOOPBACK;
}

void netadr_t::Clear()
{
	ip[0] = ip[1] = ip[2] = ip[3] = 0;
	Q_memset( ip6, 0, sizeof(ip6) );
	port = 0;
	type = NA_NULL;
}

void netadr_t::SetIP(uint8 b1, uint8 b2, uint8 b3, uint8 b4)
{
	ip[0] = b1;
	ip[1] = b2;
	ip[2] = b3;
	ip[3] = b4;
}

void netadr_t::SetIP(uint unIP)
{
	*((uint*)ip) = BigLong( unIP );
}

void netadr_t::SetIP6( const uint8 b[16] )
{
	Q_memcpy( ip6, b, sizeof(ip6) );
	type = NA_IP6;
}

bool netadr_t::IsIPv6() const
{
	return ( type == NA_IP6 );
}

void netadr_t::SetType(netadrtype_t newtype)
{
	type = newtype;
}

netadrtype_t netadr_t::GetType() const
{
	return type;
}

unsigned short netadr_t::GetPort() const
{
	return BigShort( port );
}

unsigned int netadr_t::GetIPNetworkByteOrder() const
{
	return *(unsigned int *)&ip;
}

unsigned int netadr_t::GetIPHostByteOrder() const
{
	return BigDWord( GetIPNetworkByteOrder() );
}

void netadr_t::ToSockadr (struct sockaddr * s) const
{
	Q_memset ( s, 0, sizeof(struct sockaddr));

	if (type == NA_BROADCAST)
	{
		// Broadcast is an IPv4 concept; IPv6 LAN discovery would use multicast,
		// which the engine does not do today. Keep this v4.
		((struct sockaddr_in*)s)->sin_family = AF_INET;
		((struct sockaddr_in*)s)->sin_port = port;
		((struct sockaddr_in*)s)->sin_addr.s_addr = INADDR_BROADCAST;
	}
	else if (type == NA_IP)
	{
		((struct sockaddr_in*)s)->sin_family = AF_INET;
		((struct sockaddr_in*)s)->sin_addr.s_addr = *(int *)&ip;
		((struct sockaddr_in*)s)->sin_port = port;
	}
	else if (type == NA_IP6)
	{
		struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)s;
		s6->sin6_family = AF_INET6;
		s6->sin6_port = port;
		Q_memcpy( &s6->sin6_addr, ip6, 16 );
		// scope_id left as 0; callers that need a scope must set it themselves.
	}
	else if (type == NA_LOOPBACK )
	{
		((struct sockaddr_in*)s)->sin_family = AF_INET;
		((struct sockaddr_in*)s)->sin_port = port;
		((struct sockaddr_in*)s)->sin_addr.s_addr = INADDR_LOOPBACK ;
	}
}

bool netadr_t::SetFromSockadr(const struct sockaddr * s)
{
	if (s->sa_family == AF_INET)
	{
		type = NA_IP;
		*(int *)&ip = ((struct sockaddr_in *)s)->sin_addr.s_addr;
		port = ((struct sockaddr_in *)s)->sin_port;
		return true;
	}
#if defined(POSIX)
	else if (s->sa_family == AF_INET6)
	{
		const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)s;
		const uint8 *b = (const uint8 *)&s6->sin6_addr;

		// IPv4-mapped IPv6 addresses (::ffff:a.b.c.d) are normalized to NA_IP so
		// the rest of the engine (which is IPv4-centric) keeps working when a
		// dual-stack socket hands us a v4 peer in v6 clothing.
		if ( b[0]==0 && b[1]==0 && b[2]==0 && b[3]==0 &&
			 b[4]==0 && b[5]==0 && b[6]==0 && b[7]==0 &&
			 b[8]==0 && b[9]==0 && b[10]==0xff && b[11]==0xff )
		{
			type = NA_IP;
			ip[0] = b[12]; ip[1] = b[13]; ip[2] = b[14]; ip[3] = b[15];
			port = s6->sin6_port;
			return true;
		}

		type = NA_IP6;
		Q_memcpy( ip6, b, 16 );
		port = s6->sin6_port;
		return true;
	}
#endif // POSIX
	else
	{
		Clear();
		return false;
	}
}

bool netadr_t::IsValid() const
{
	if ( type == NA_NULL )
		return false;
	if ( port == 0 )
		return false;
	if ( type == NA_IP )
		return ( ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0 );
	if ( type == NA_IP6 )
	{
		for ( int i = 0; i < 16; i++ )
			if ( ip6[i] != 0 )
				return true;
		return false;
	}
	// NA_LOOPBACK, NA_BROADCAST
	return true;
}

bool netadr_t::IsBaseAdrValid() const
{
	if ( type == NA_NULL )
		return false;
	if ( type == NA_IP )
		return ( ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0 );
	if ( type == NA_IP6 )
	{
		for ( int i = 0; i < 16; i++ )
			if ( ip6[i] != 0 )
				return true;
		return false;
	}
	// NA_LOOPBACK, NA_BROADCAST
	return true;
}

#ifdef _WIN32
#undef SetPort	// get around stupid WINSPOOL.H macro
#endif

void netadr_t::SetPort(unsigned short newport)
{
	port = BigShort( newport );
}

bool netadr_t::SetFromString( const char *pch, bool bUseDNS )
{
	Clear();

	Assert( pch );		// invalid to call this with NULL pointer; fix your code bug!
	if ( !pch )			// but let's not crash
		return false;

	char address[ 128 ];
	V_strcpy_safe( address, pch );

	if ( !V_strnicmp( address, "loopback", 8 ) )
	{
		char newaddress[ 128 ];
		type = NA_LOOPBACK;
		V_strcpy_safe( newaddress, "127.0.0.1" );
		V_strcat_safe( newaddress, address + 8 ); // copy anything after "loopback"

		V_strcpy_safe( address, newaddress );
	}

	if ( !V_strnicmp( address, "localhost", 9 ) )
	{
		V_memcpy( address, "127.0.0.1", 9 ); // Note use of memcpy allows us to keep the colon and rest of string since localhost and 127.0.0.1 are both 9 characters.
	}

#if defined(POSIX)
	// IPv6 literal with optional bracketing:  [::1]:port  or  [::1]  or  ::1:port
	// (the last form is ambiguous; require brackets when a port is given for v6)
	if ( address[0] == '[' )
	{
		char *close = strchr( address, ']' );
		if ( !close )
			return false;

		char addr6[ 64 ];
		int addrLen = close - ( address + 1 );
		if ( addrLen <= 0 || addrLen >= (int)sizeof(addr6) )
			return false;
		V_memcpy( addr6, address + 1, addrLen );
		addr6[ addrLen ] = 0;

		uint8 bytes[16];
		if ( inet_pton( AF_INET6, addr6, bytes ) != 1 )
			return false;

		SetIP6( bytes );

		// Optional :port after ']'
		if ( close[1] == ':' )
		{
			SetPort( (uint16) V_atoi( close + 2 ) );
		}
		else if ( close[1] == 0 )
		{
			SetPort( 0 );
		}
		else
		{
			return false;
		}
		return true;
	}

	// Bare IPv6 literal (no brackets), no port:  ::1, fe80::1, 2001:db8::1, ::ffff:1.2.3.4
	// A dotted-quad-with-port like "1.2.3.4:5" also has a colon, so inet_pton will
	// simply fail on it and we fall through to the IPv4 branch below.
	if ( strchr( address, ':' ) )
	{
		uint8 bytes[16];
		if ( inet_pton( AF_INET6, address, bytes ) == 1 )
		{
			SetIP6( bytes );
			SetPort( 0 );
			return true;
		}
	}
#endif // POSIX

	// Starts with a number and has a dot -> dotted-quad IPv4, optional :port
	if ( address[0] >= '0' && 
		 address[0] <= '9' && 
		 strchr( address, '.' ) )
	{
		int n1 = -1, n2 = -1, n3 = -1, n4 = -1, n5 = 0; // set port to 0 if we don't parse one
		int nRes = sscanf( address, "%d.%d.%d.%d:%d", &n1, &n2, &n3, &n4, &n5 );
		if (
			nRes < 4
			|| n1 < 0 || n1 > 255
			|| n2 < 0 || n2 > 255
			|| n3 < 0 || n3 > 255
			|| n4 < 0 || n4 > 255
			|| n5 < 0 || n5 > 65535
		)
			return false;
		SetIP( n1, n2, n3, n4 );
		SetPort( ( uint16 ) n5 );
		return true;
	}

	if ( bUseDNS )
	{
// X360TBD:
	// dgoodenough - since this is skipped on X360, seems reasonable to skip as well on PS3
	// PS3_BUILDFIX
	// FIXME - Leap of faith, this works without asserting on X360, so I assume it will on PS3
#if defined(POSIX)
		// Resolve via getaddrinfo (dual-stack: AF_UNSPEC returns v6 or v4 as available;
		// the result is fed through SetFromSockadr which normalizes v4-mapped -> NA_IP).
		// Strip an optional :port from the input first.
		char host[ 128 ];
		V_strcpy_safe( host, address );
		uint16 usPort = 0;
		char *pchColon = strchr( host, ':' );
		if ( pchColon )
		{
			*pchColon = 0;
			usPort = (uint16) V_atoi( pchColon + 1 );
		}

		struct addrinfo hints;
		Q_memset( &hints, 0, sizeof(hints) );
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = AI_ADDRCONFIG;
		struct addrinfo *res = NULL;
		if ( getaddrinfo( host, NULL, &hints, &res ) != 0 || !res )
			return false;

		bool bOk = SetFromSockadr( res->ai_addr );
		freeaddrinfo( res );
		if ( !bOk )
			return false;

		if ( usPort )
			SetPort( usPort );
		return true;
#elif !defined( _X360 ) && !defined( _PS3 )
		// Winsock1 path: gethostbyname returns IPv4 only (Windows build stays v4).
		char *pchColon = strchr( address, ':' );
		if ( pchColon )
		{
			*pchColon = 0;
		}
		
		struct hostent *h = gethostbyname( address );
		if ( !h )
			return false;

		SetIP( ntohl( *(int *)h->h_addr_list[0] ) );

		if ( pchColon )
		{
			SetPort( V_atoi( ++pchColon ) );
		}
		return true;
#else
		Assert( 0 );
		return false;
#endif
	}

	return false;
}

bool netadr_t::operator<(const netadr_t &netadr) const
{
	// Order by type first so NA_IP and NA_IP6 entries never compare as equal-ish.
	if ( type != netadr.type )
		return ( type < netadr.type );

	if ( type == NA_IP )
	{
		if ( *((uint *)netadr.ip) < *((uint *)ip) )
			return true;
		else if ( *((uint *)netadr.ip) > *((uint *)ip) )
			return false;
		return ( netadr.port < port );
	}

	if ( type == NA_IP6 )
	{
		int cmp = Q_memcmp( netadr.ip6, ip6, sizeof(ip6) );
		if ( cmp < 0 )
			return true;
		else if ( cmp > 0 )
			return false;
		return ( netadr.port < port );
	}

	// NA_NULL / NA_LOOPBACK / NA_BROADCAST: fall back to port ordering.
	return ( netadr.port < port );
}


void netadr_t::SetFromSocket( int hSocket )
{	
	// dgoodenough - since this is skipped on X360, seems reasonable to skip as well on PS3
	// PS3_BUILDFIX
	// FIXME - Leap of faith, this works without asserting on X360, so I assume it will on PS3
#if !defined( _X360 ) && !defined( _PS3 )
	Clear();
	type = NA_IP;

	struct sockaddr address;
	socklen_t namelen = sizeof(address);
	if ( getsockname( hSocket, (struct sockaddr *)&address, &namelen) == 0 )
	{
		SetFromSockadr( &address );
	}
#else
	Assert(0);
#endif
}
