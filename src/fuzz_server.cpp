#include "signedauthserver.hpp"
#include "rrgenerator.hpp"
#include "shufflebytes.hpp"
#include "logger.hpp"
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <random>

namespace dns
{
    class FuzzServer : public SignedAuthServer
    {
    public:
	FuzzServer( const DNSServerParameters &params, const Domainname &hint2 = Domainname() )
	    : dns::SignedAuthServer( params ), 
	      mAnotherHint( hint2 )
	{
        }

        std::vector<ResourceRecord> newRRs( const RRSet &rrset ) const
        {
            std::vector<ResourceRecord> rrs;
            rrset.addResourceRecords( rrs );
            return rrs;
        }

        MessageInfo modifyResponse( const MessageInfo &query,
				    const MessageInfo &original_response,
				    bool via_tcp ) const
        {
            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "modifying response: " << original_response;
            MessageInfo modified_response = original_response;

            ResourceRecordGenerator rr_generator;

            // clear rr
            if ( withChance( 0.03 ) )
                modified_response.clearAnswerSection();
            if ( withChance( 0.03 ) )
                modified_response.clearAuthoritySection();
            if ( withChance( 0.03 ) )
                modified_response.clearAdditionalSection();

            // appand new rrsets
            unsigned int rrsets_count = getRandom( 16 );
            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "generating rr : size = " << rrsets_count;
            for ( unsigned int i = 0 ; i < rrsets_count ; i++ ) {
                RRSet rrset = rr_generator.generate( original_response, mAnotherHint );

                switch ( getRandom( 4 ) ) {
                case 0:
                    {
                        auto new_rrs = newRRs( rrset );
                        for ( auto rr : new_rrs )
                            modified_response.pushAnswerSection( rr );
                    }
                    break;
                case 1:
                    {
                        auto new_rrs = newRRs( rrset );
                        for ( auto rr : new_rrs )
                            modified_response.pushAuthoritySection( rr );
                    }
                    break;
                case 2:
                    {
                        auto new_rrs = newRRs( rrset );
                        for ( auto rr : new_rrs )
                            modified_response.pushAdditionalSection( rr );
                    }
                    break;
                default:
                    break;
                }
            }
            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "generated rr : size = " << rrsets_count;

            replaceClass( modified_response.mAnswerSection );
            replaceClass( modified_response.mAuthoritySection );
            replaceClass( modified_response.mAdditionalSection );

	    int sign_count;
	    sign_count= getRandom( 1 );
	    for ( int i = 0 ; i < sign_count ; i++ )
		signSection( modified_response.mAnswerSection );
	    sign_count= getRandom( 1 );
	    for ( int i = 0 ; i < sign_count ; i++ )
		signSection( modified_response.mAuthoritySection );
	    sign_count= getRandom( 1 );
	    for ( int i = 0 ; i < sign_count ; i++ )
		signSection( modified_response.mAdditionalSection );

	    OptionGenerator option_generator;
	    unsigned int option_count = getRandom( 3 );
            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "generating options : size = " << option_count;
	    for ( unsigned int i = 0 ; i < option_count ; i++ )
		option_generator.generate( modified_response );

            if ( withChance( 0.1 ) ) {
                modified_response.mOptPseudoRR.mPayloadSize = getRandom( 0xffff  );
            }
            if ( withChance( 0.1 ) ) {
                modified_response.mOptPseudoRR.mRCode = getRandom( 16 );
            }
            if ( withChance( 0.1 ) ) {
                modified_response.mOptPseudoRR.mDOBit = getRandom( 1 );
            }
            if ( withChance( 0.1 ) ) {
                modified_response.mOptPseudoRR.mVersion = getRandom( 0xff );
            }
            if ( withChance( 0.05 ) ) {
                modified_response.mOptPseudoRR.mDomainname = generateDomainname();
            }
            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "generated options : size = " << option_count;
	    
            if ( withChance( 0.2 ) ) {
                BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "adding options : size = " << option_count;
                ResourceRecord opt_pseudo_rr = generateOptPseudoRecord( modified_response.mOptPseudoRR );
                RRSet rrset( opt_pseudo_rr.mDomainname,
                             opt_pseudo_rr.mClass,
                             opt_pseudo_rr.mType,
                             opt_pseudo_rr.mTTL );

                std::shared_ptr<RRSet> rrsig = signRRSet( rrset );
                rrsig->addResourceRecords( modified_response.mAdditionalSection );
                BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "added options : size = " << option_count;
            }

            if ( withChance( 0.1 ) ) {
                modified_response.mResponseCode = getRandom( 16 );
            }

            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "shuffling RR";
/*            if ( withChance( 0.2 ) )
                shuffle_rr( modified_response.mAnswerSection );

            if ( withChance( 0.2 ) )
                shuffle_rr( modified_response.mAuthoritySection );

            if ( withChance( 0.2 ) )
                shuffle_rr( modified_response.mAdditionalSection );
		*/
            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "shuffled RR";

            if ( withChance( 0.05 ) )
                modified_response.mQueryResponse = getRandom( 2 );
            if ( withChance( 0.05 ) )
                modified_response.mOpcode = getRandom( 2 );
            if ( withChance( 0.05 ) )
                modified_response.mAuthoritativeAnswer = getRandom( 2 );
            if ( withChance( 0.02 ) )
                modified_response.mTruncation = getRandom( 2 );
            if ( withChance( 0.05 ) )
                modified_response.mRecursionDesired = getRandom( 2 );
            if ( withChance( 0.05 ) )
                modified_response.mRecursionAvailable = getRandom( 2 );
            if ( withChance( 0.05 ) )
                modified_response.mCheckingDisabled = getRandom( 2 );
            if ( withChance( 0.05 ) )
                modified_response.mZeroField = getRandom( 0x07 );
            if ( withChance( 0.05 ) )
                modified_response.mAuthenticData = getRandom( 2 );
            if ( withChance( 0.05 ) )
                modified_response.mResponseCode = getRandom( 0x0f );

            uint32_t requested_max_payload_size = 512;
            if ( query.isEDNS0() &&
                 query.mOptPseudoRR.mPayloadSize > 512 ) {
                requested_max_payload_size = query.mOptPseudoRR.mPayloadSize;
            }

            std::cerr << "getting Message Size" << std::endl; 
            if ( ! via_tcp &&
                 modified_response.getMessageSize() > requested_max_payload_size ) {
                if ( withChance( 0.05 ) ) {
                    modified_response.mTruncation = 1;
                    modified_response.clearAnswerSection();
                    modified_response.clearAuthoritySection();
                    modified_response.clearAdditionalSection();
                }
            }
            std::cerr << "got Message Size" << std::endl; 

            BOOST_LOG_TRIVIAL(trace) << "dns.server.fuzzing: " << "modified response";
            return modified_response;
        }

	void modifyMessage( const MessageInfo &query, WireFormat &message ) const
	{
            unsigned int shuffle_count = getRandom( 1 );
            for ( unsigned int i = 0 ; i < shuffle_count ; i++ ) {
                WireFormat src = message;
                dns::shuffle( src, message );
            }
	}
	
        void shuffle_rr( std::vector<ResourceRecord> &rrs ) const
        {
            static boost::thread_specific_ptr<std::mt19937> tls_random;
            if ( ! tls_random.get() ) {
		tls_random.reset( new std::mt19937 );
            }
	    std::shuffle( rrs.begin(), rrs.end(), *tls_random );
        }

        void replaceClass( std::vector<ResourceRecord> &section ) const
        {
            if ( getRandom( 5 ) )
                return;

            Class class_table[] = { CLASS_IN, CLASS_CH, CLASS_HS, CLASS_NONE, CLASS_ANY };
            for ( ResourceRecord &rr : section ) {
                unsigned int index = getRandom( sizeof(class_table)/sizeof(Class) - 1 );
                if ( index >= sizeof(class_table)/sizeof(Class) ) {
                    std::cerr << "invalid replace class index " << index << "." << std::endl;
                    throw std::logic_error( "invalid replace class index" );
                }
                rr.mClass = class_table[ index ];
            }
        }

        void signSection( std::vector<ResourceRecord> &section ) const
        {
            std::vector<ResourceRecord> rrsigs;
            std::vector< std::shared_ptr<RRSet> > signed_targets = cumulate( section );
            for ( auto signed_target : signed_targets ) {
                std::shared_ptr<RRSet> rrsig_rrset = signRRSet( *signed_target );
                rrsig_rrset->addResourceRecords( section );
            }
            section.insert( section.end(), rrsigs.begin(), rrsigs.end() );
        }

        std::vector<std::shared_ptr<RRSet> > cumulate( const std::vector<ResourceRecord> &rrs ) const
        {
            std::vector<std::shared_ptr<RRSet> > rrsets;

            for ( auto rr : rrs ) {
                bool is_found = false;
                for ( auto rrset : rrsets ) {
                    if ( rr.mDomainname == rrset->getOwner() &&
                         rr.mClass      == rrset->getClass() && 
                         rr.mType       == rrset->getType()  ) {
                        rrset->add( std::shared_ptr<RDATA>( rr.mRData->clone() ) );
                        is_found = true;
                        break;
                    }
                }
                if ( ! is_found ) {
                    std::shared_ptr<RRSet> new_rrset( std::shared_ptr<RRSet>( new RRSet( rr.mDomainname, rr.mClass, rr.mType, rr.mTTL ) ) );
                    new_rrset->add( std::shared_ptr<RDATA>( rr.mRData->clone() ) );
                    rrsets.push_back( new_rrset );
                }
            }

            return rrsets;
        }

    private:
        Domainname mAnotherHint;
    };


}

int main( int argc, char **argv )
{
    namespace po = boost::program_options;

    std::string bind_address;
    uint16_t    bind_port;
    uint16_t    thread_count; 
    std::string zone_filename;
    std::string apex;
    std::string log_level;
    std::string ksk_filename, zsk_filename;
    bool                 enable_nsec;
    bool                 enable_nsec3;
    std::vector<uint8_t> nsec3_salt;
    std::string          nsec3_salt_str;
    uint16_t             nsec3_iterate;
    uint16_t             nsec3_hash_algo;
    std::string          another_hint;
    
    po::options_description desc( "fuzz server" );
    desc.add_options()( "help,h", "print this message" )

        ( "bind,b",       po::value<std::string>( &bind_address )->default_value( "0.0.0.0" ), "bind address" )
        ( "port,p",       po::value<uint16_t>( &bind_port )->default_value( 53 ),              "bind port" )
        ( "thread,n",     po::value<uint16_t>( &thread_count )->default_value( 1 ),            "thread count" )
	( "multicast,m",                                                                       "multicast" )  
	( "file,f",       po::value<std::string>( &zone_filename ),                            "zone filename" )
	( "zone,z",       po::value<std::string>( &apex),                                      "zone apex" )
	( "another,a",    po::value<std::string>( &another_hint ),                             "another domainname for cache poisoning" )
        ( "ksk,K",        po::value<std::string>( &ksk_filename),                              "KSK filename" )
        ( "zsk,Z",        po::value<std::string>( &zsk_filename),                              "ZSK filename" )
        ( "nsec",         po::value<bool>( &enable_nsec )->default_value( true ),              "enable NSEC" )
        ( "nsec3,3",      po::value<bool>( &enable_nsec3 )->default_value( false ),            "enable NSEC3" )
        ( "salt,s",       po::value<std::string>( &nsec3_salt_str )->default_value( "00" ),    "NSEC3 salt" )
        ( "iterate,i",    po::value<uint16_t>( &nsec3_iterate )->default_value( 1 ),           "NSEC3 iterate" )
        ( "hash",         po::value<uint16_t>( &nsec3_hash_algo )->default_value( 1 ),         "NSEC3 hash algorithm" )
        ( "log-leevel,l", po::value<std::string>( &log_level )->default_value( "info" ),  "trace|debug|info|warning|error|fatal" )
	;
    
    po::variables_map vm;
    po::store( po::parse_command_line( argc, argv, desc ), vm );
    po::notify( vm );

    if ( vm.count( "help" ) ) {
        std::cerr << desc << "\n";
        return 1;
    }

    dns::logger::initialize( log_level );
    
    if ( apex.back() != '.' )
	apex.push_back( '.' );

    decodeFromHex( nsec3_salt_str, nsec3_salt );

    dns::getRandom();
    
    try {
	dns::DNSServerParameters params;
	params.mBindAddress = bind_address;
	params.mBindPort    = bind_port;
	params.mMulticast   = vm.count( "multicast" ) > 0;
	params.mThreadCount = thread_count;
	dns::FuzzServer server( params, (dns::Domainname)another_hint );
	server.load( apex, zone_filename,
                     ksk_filename, zsk_filename,
                     nsec3_salt, nsec3_iterate, dns::DNSSEC_SHA1,
                     enable_nsec, enable_nsec3 );
        std::vector<std::shared_ptr<dns::RecordDS>> rrset_ds = server.getDSRecords();
	std::cout << "DS records" << std::endl;
        for ( auto ds : rrset_ds ){
            std::cout << apex << "   IN DS " << ds->toZone() << std::endl;
        }

	BOOST_LOG_TRIVIAL(info) << "Starting Fuzzing Server on " << bind_address << ":" << bind_port << ".";
	server.start();
    }
    catch ( std::runtime_error &e ) {
	std::cerr << e.what() << std::endl;
    }
    catch ( std::logic_error &e ) {
	std::cerr << e.what() << std::endl;
    }
    return 0;
}
