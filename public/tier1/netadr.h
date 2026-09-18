//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// netadr.h
#ifndef NETADR_H
#define NETADR_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"
#undef SetPort

class bf_read;
class bf_write;

typedef enum
{ 
	NA_NULL = 0,
	NA_LOOPBACK,
	NA_BROADCAST,
	NA_IP,
	NA_IP6,		// IPv6 address (stored in ip6[]; ip[] is unused). IPv4-mapped
				// addresses are normalized to NA_IP, so NA_IP6 always means a
				// true IPv6 peer.
} netadrtype_t;

typedef struct netadr_s
{
public:
	// Note: ip6 is zeroed here without Q_memset, because netadr.h is included
	// before strtools.h (which defines Q_memset) in some translation units.
	netadr_s() { for ( int i = 0; i < 16; i++ ) ip6[i] = 0; SetIP( 0 ); SetPort( 0 ); SetType( NA_IP ); }
	netadr_s( uint unIP, uint16 usPort ) { for ( int i = 0; i < 16; i++ ) ip6[i] = 0; SetIP( unIP ); SetPort( usPort ); SetType( NA_IP ); }
	netadr_s( const char *pch ) { for ( int i = 0; i < 16; i++ ) ip6[i] = 0; SetIP( 0 ); SetPort( 0 ); SetType( NA_IP ); SetFromString( pch ); }
	void	Clear();	// invalids Address

	void	SetType( netadrtype_t type );
	void	SetPort( unsigned short port );
	bool	SetFromSockadr(const struct sockaddr *s);
	void	SetIP(uint8 b1, uint8 b2, uint8 b3, uint8 b4);
	void	SetIP(uint unIP);									// Sets IP.  unIP is in host order (little-endian)
	void    SetIPAndPort( uint unIP, unsigned short usPort ) { SetIP( unIP ); SetPort( usPort ); }
	void	SetIP6( const uint8 b[16] );						// Sets the IPv6 address (host byte order, network-format bytes). Type becomes NA_IP6.
	bool	SetFromString(const char *pch, bool bUseDNS = false ); // if bUseDNS is true then do a DNS lookup if needed
	
	bool	CompareAdr (const netadr_s &a, bool onlyBase = false) const;
	bool	CompareClassBAdr (const netadr_s &a) const;
	bool	CompareClassCAdr (const netadr_s &a) const;

	netadrtype_t	GetType() const;
	unsigned short	GetPort() const;
	bool			IsIPv6() const;		// true if this is an NA_IP6 address

	// DON'T CALL THIS
	const char*		ToString( bool onlyBase = false ) const; // returns xxx.xxx.xxx.xxx:ppppp

	void	ToString( char *pchBuffer, uint32 unBufferSize, bool onlyBase = false ) const; // returns xxx.xxx.xxx.xxx:ppppp
	template< size_t maxLenInChars >
	void	ToString_safe( char (&pDest)[maxLenInChars], bool onlyBase = false ) const
	{
		ToString( &pDest[0], maxLenInChars, onlyBase );
	}

	void			ToSockadr(struct sockaddr *s) const;

	// Returns 0xAABBCCDD for AA.BB.CC.DD on all platforms, which is the same format used by SetIP().
	// (So why isn't it just named GetIP()?  Because previously there was a fucntion named GetIP(), and
	// it did NOT return back what you put into SetIP().  So we nuked that guy.)
	unsigned int	GetIPHostByteOrder() const;

	// Returns a number that depends on the platform.  In most cases, this probably should not be used.
	unsigned int	GetIPNetworkByteOrder() const;

	bool	IsLocalhost() const; // true, if this is the localhost IP 
	bool	IsLoopback() const;	// true if engine loopback buffers are used
	bool	IsReservedAdr() const; // true, if this is a private LAN IP
	bool	IsValid() const;	// ip & port != 0
	bool	IsBaseAdrValid() const;	// ip != 0

	void    SetFromSocket( int hSocket );

	bool	Unserialize( bf_read &readBuf );
	bool	Serialize( bf_write &writeBuf );

	bool operator==(const netadr_s &netadr) const {return ( CompareAdr( netadr ) );}
	bool operator!=(const netadr_s &netadr) const {return !( CompareAdr( netadr ) );}
	bool operator<(const netadr_s &netadr) const;

public:	// members are public to avoid to much changes

	netadrtype_t	type;
	unsigned char	ip[4];		// IPv4 address (NA_IP). Unused when type == NA_IP6.
	unsigned char	ip6[16];	// IPv6 address (NA_IP6). Unused when type == NA_IP.
	unsigned short	port;
} netadr_t;


/// Helper class to render a netadr_t.  Use this when formatting a net address
/// in a printf.  Don't use adr.ToString()!
class CUtlNetAdrRender
{
public:
	CUtlNetAdrRender( const netadr_t &obj, bool bBaseOnly = false )
	{
		obj.ToString( m_rgchString, sizeof(m_rgchString), bBaseOnly );
	}

	CUtlNetAdrRender( uint32 unIP )
	{
		netadr_t addr( unIP, 0 );
		addr.ToString( m_rgchString, sizeof(m_rgchString), true );
	}

	CUtlNetAdrRender( uint32 unIP, uint16 unPort )
	{
		netadr_t addr( unIP, unPort );
		addr.ToString( m_rgchString, sizeof(m_rgchString), false );
	}

	CUtlNetAdrRender( const struct sockaddr &s )
	{
		netadr_t addr;
		if ( addr.SetFromSockadr( &s ) )
			addr.ToString( m_rgchString, sizeof(m_rgchString), false );
		else
			m_rgchString[0] = '\0';
	}

	const char * String() const
	{ 
		return m_rgchString;
	}

private:

	char m_rgchString[64];	// large enough for "[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535"
};

#endif // NETADR_H
