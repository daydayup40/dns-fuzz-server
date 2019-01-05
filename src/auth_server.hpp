#ifndef AUTH_SERVER_HPP
#define AUTH_SERVEE_HPP

#include "dns_server.hpp"
#include "zone.hpp"
#include "zoneloader.hpp"

namespace dns
{
    class AuthServer : public dns::DNSServer
    {
    public:
	AuthServer( const std::string &addr, uint16_t port, bool debug )
	    : dns::DNSServer( addr, port, true, debug )
	{}

	void load( const std::string &apex, const std::string &filename );
	MessageInfo generateResponse( const MessageInfo &query, bool via_tcp ) const;
	virtual MessageInfo modifyResponse( const MessageInfo &query,
					    const MessageInfo &original_response,
					    bool vir_tcp ) const;

    private:
	std::shared_ptr<Zone> zone;
    };
}

#endif
