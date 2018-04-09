#include "signedzoneimp.hpp"
#include <algorithm>
#include <sstream>
#include <iterator>

namespace dns
{

    SignedZoneImp::SignedZoneImp( const Domainname &zone_name, const std::string &ksk_config, const std::string &zsk_config )
        : mApex( zone_name ), mSigner( zone_name, ksk_config, zsk_config )
    {
	mOwnerToNode.insert( OwnerToNodePair( mApex, NodePtr( new Node ) ) );
    }

    void SignedZoneImp::addEmptyNode( const Domainname &domainname )
    {
        mOwnerToNode.insert( OwnerToNodePair( domainname, NodePtr( new Node ) ) );
    }

    void SignedZoneImp::add( RRSetPtr rrset )
    {
        Domainname owner = rrset->getOwner();
        if ( ! mApex.isSubDomain( owner ) ) {
            throw std::runtime_error( "owner " + owner.toString() + "is not contained in " + mApex.toString() );
        }

	Domainname relative_name = mApex.getRelativeDomainname( owner );
	Domainname node_name     = mApex;
	for ( auto r = relative_name.getCanonicalLabels().rbegin() ; r != relative_name.getCanonicalLabels().rend() ; ++r ) {
	    node_name.addSubdomain( *r );
	    if ( ! findNode( node_name ) )
		addEmptyNode( node_name );
	}
	auto node = findNode( owner );
	if ( node.get() == nullptr )
	    throw std::logic_error( "node must be exist" );
	node->add( rrset );

	if ( rrset->getType() == TYPE_SOA && rrset->getOwner() == mApex ) {
	    mSOA = rrset;
	}
	if ( rrset->getType() == TYPE_NS && rrset->getOwner() == mApex ) {
	    mNameServers = rrset;
	}	
    }

    PacketInfo SignedZoneImp::getAnswer( const PacketInfo &query ) const
    {
        if ( query.mQuestionSection.size() != 1 ) {
            throw std::logic_error( "one qname must be exist" );
        }
	
        Domainname qname  = query.mQuestionSection[0].mDomainname;
        Type       qtype  = query.mQuestionSection[0].mType;
        Class      qclass = query.mQuestionSection[0].mClass;

        PacketInfo response;

        response.mID                  = query.mID;
        response.mOpcode              = query.mOpcode;
        response.mQueryResponse       = 1;
        response.mAuthoritativeAnswer = 1;
        response.mTruncation          = 0;
        response.mRecursionDesired    = query.mRecursionDesired;
        response.mRecursionAvailable  = 0;
        response.mZeroField           = 0;
        response.mAuthenticData       = 1;
        response.mCheckingDisabled    = query.mCheckingDisabled;

	if ( query.isEDNS0() ) {
	    OptPseudoRecord opt;
	    opt.mPayloadSize = std::min<uint16_t>( 1280, query.mOptPseudoRR.mPayloadSize );
	    opt.mDOBit = query.mOptPseudoRR.mDOBit;
	    response.mIsEDNS0 = true;
	    response.mOptPseudoRR = opt;
	}

        QuestionSectionEntry q;
        q.mDomainname = qname;
        q.mType       = qtype;
        q.mClass      = qclass;
        response.mQuestionSection.push_back( q );

        if ( ! mApex.isSubDomain( qname ) ) {
            response.mResponseCode = REFUSED;
            return response;
        }

	// QTYPE == DNSKEY, return RRSet from ZoneSigner
	if ( qtype == TYPE_DNSKEY && qname == mSOA->getOwner() ) {
	    std::vector<std::shared_ptr<RecordDNSKEY>> keys = mSigner.getDNSKEYRecords();
	    std::shared_ptr<RRSet> dnskey_rrset( new RRSet( mSOA->getOwner(), mSOA->getClass(), TYPE_DNSKEY, mSOA->getTTL() ) );
	    for ( auto k : keys ) {
		dnskey_rrset->add( k );
	    }
            addRRSet( response.mAnswerSection, *dnskey_rrset );
            if ( response.isDNSSECOK() ) {
                std::shared_ptr<RRSet> rrsig_rrset = mSigner.signDNSKEY( mSOA->getTTL() );
                addRRSet( response.mAnswerSection, *rrsig_rrset );
            }
	    return response;
	}

        if ( qtype == TYPE_RRSIG ) {
            auto node = findNode( qname );
            if ( node ) {
                if ( node->exist() ) {
                    for ( auto rrset_itr = node->begin() ; rrset_itr != node->end() ; rrset_itr++ ) {
                        auto rrset = *(rrset_itr->second);
                        std::shared_ptr<RRSet> rrsig = mSigner.signRRSet( rrset );
                        addRRSet( response.mAnswerSection, *rrsig );
                    }
                }
                else {
                    // NoData ( found empty non-terminal or other type )
                    response.mResponseCode = NO_ERROR;
                    addSOAToAuthoritySection( response );
                    if ( response.isDNSSECOK() ) {
                        RRSetPtr nsec = generateNSECRRSet( qname );
                        addRRSet( response.mAuthoritySection, *nsec );
                        addRRSIG( response.mAuthoritySection, *nsec );

                        Domainname wildcard = mApex;
                        wildcard.addSubdomain( "*" );
                        RRSetPtr wildcard_nsec = generateNSECRRSet( wildcard );
                        addRRSet( response.mAuthoritySection, *wildcard_nsec );
                        addRRSIG( response.mAuthoritySection, *wildcard_nsec );
                    }
		}
		return response;
            }
            else {
                // NXDOMAIN
                response.mResponseCode = NXDOMAIN;
                addSOAToAuthoritySection( response );
                if ( response.isDNSSECOK() ) {
                    RRSetPtr nsec = generateNSECRRSet( qname );
                    addRRSet( response.mAuthoritySection, *nsec );
                    addRRSIG( response.mAuthoritySection, *nsec );

                    Domainname wildcard = mApex;
                    wildcard.addSubdomain( "*" );
                    RRSetPtr wildcard_nsec = generateNSECRRSet( wildcard );
                    addRRSet( response.mAuthoritySection, *wildcard_nsec );
                    addRRSIG( response.mAuthoritySection, *wildcard_nsec );
                }
                return response;
            }
        }

        // find DNAME
        for ( auto parent_name = qname ; parent_name != mApex ; parent_name.popSubdomain() ) {
            auto dname_rrset = findRRSet( parent_name, TYPE_DNAME );
            if ( dname_rrset ) {
                if ( dname_rrset->count() != 1 )
                    throw std::logic_error( "multiple DNAME records exist " + parent_name.toString() );
                response.mResponseCode = NO_ERROR;
                addRRSet( response.mAnswerSection, *dname_rrset );
                if ( response.isDNSSECOK() ) {
                    addRRSIG( response.mAnswerSection, *dname_rrset );
                }

                auto dname_rdata    = std::dynamic_pointer_cast<RecordDNAME>( (*dname_rrset)[0] );
                auto relative_name  = parent_name.getRelativeDomainname( qname );
                auto canonical_name = relative_name + dname_rdata->getCanonicalName();
                std::shared_ptr<RecordCNAME> cname_rdata( new RecordCNAME( canonical_name ) );
                RRSet cname_rrset( canonical_name, dname_rrset->getClass(), TYPE_CNAME, dname_rrset->getTTL() );
                cname_rrset.add( cname_rdata );

                addRRSet( response.mAnswerSection, cname_rrset );
                if ( response.isDNSSECOK() ) {
                    addRRSIG( response.mAnswerSection, cname_rrset );
                }

                auto canonical_node  = findNode( canonical_name );
                if ( canonical_node ) {
                    auto canonical_rrset = canonical_node->find( qtype );
                    if ( canonical_rrset ) {
                        addRRSet( response.mAnswerSection, *canonical_rrset );
                        if ( response.isDNSSECOK() ) {
                            addRRSIG( response.mAnswerSection, *canonical_rrset );
                        }
                    }
                }
                return response;
            }   
        }

	// find qname
        auto node = findNode( qname );
        if ( node ) {
	    if ( qtype == TYPE_ANY ) {
		if ( node->exist() ) {
		    for ( auto rrset_itr = node->begin() ; rrset_itr != node->end() ; rrset_itr++ ) {
                        auto rrset = *(rrset_itr->second);
                        addRRSet( response.mAnswerSection, rrset );
                        if ( response.isDNSSECOK() ) {
                            addRRSIG( response.mAuthoritySection, rrset );
                        }
		    }
		}
		else {
		    // NoData ( found empty non-terminal )
		    response.mResponseCode = NO_ERROR;
		    addSOAToAuthoritySection( response );
                    if ( response.isDNSSECOK() ) {
                        RRSetPtr nsec = generateNSECRRSet( qname );
                        addRRSet( response.mAuthoritySection, *nsec );
                        addRRSIG( response.mAuthoritySection, *nsec );
                    }
		}
		return response;
	    }
            auto cname_rrset = node->find( TYPE_CNAME );
            if ( cname_rrset ) {
                if ( cname_rrset->count() != 1 ) {
                    throw std::logic_error( "muliple cname records exist in " + qname.toString() );
                }
                
                // found 
                response.mResponseCode = NO_ERROR;
                addRRSet( response.mAnswerSection, *cname_rrset );
                if ( response.isDNSSECOK() ) {
                    addRRSIG( response.mAnswerSection, *cname_rrset );
                }
                std::shared_ptr<RecordCNAME> cname = std::dynamic_pointer_cast<RecordCNAME>( (*cname_rrset)[0] );
                auto canonical_name = cname->getCanonicalName();
                if (mApex.isSubDomain( canonical_name ) ) {
                    auto canonical_node  = findNode( canonical_name );
                    auto canonical_rrset = canonical_node->find( qtype );
                    if ( canonical_rrset ) {
                        addRRSet( response.mAnswerSection, *canonical_rrset );
                        if ( response.isDNSSECOK() ) {
                            addRRSIG( response.mAnswerSection, *canonical_rrset );
                        }
                    }
                }
                return response;
            }

            auto rrset = node->find( qtype );
            if ( rrset ) {
                // found 
                response.mResponseCode = NO_ERROR;
                addRRSet( response.mAnswerSection, *rrset );
                if ( response.isDNSSECOK() ) {
                    addRRSIG( response.mAnswerSection, *rrset );
                }
                return response;
            }
            else {
                // NoData ( found empty non-terminal or other type )
                response.mResponseCode = NO_ERROR;
                addSOAToAuthoritySection( response );
                if ( response.isDNSSECOK() ) {
                    RRSetPtr nsec = generateNSECRRSet( qname );
                    addRRSet( response.mAuthoritySection, *nsec );
                    addRRSIG( response.mAuthoritySection, *nsec );

                    Domainname wildcard = mApex;
                    wildcard.addSubdomain( "*" );
                    RRSetPtr wildcard_nsec = generateNSECRRSet( wildcard );
                    addRRSet( response.mAuthoritySection, *wildcard_nsec );
                    addRRSIG( response.mAuthoritySection, *wildcard_nsec );
		}
		return response;
            }
        }
        
        // NXDOMAIN
        response.mResponseCode = NXDOMAIN;
        addSOAToAuthoritySection( response );
        if ( response.isDNSSECOK() ) {
            RRSetPtr nsec = generateNSECRRSet( qname );
            addRRSet( response.mAuthoritySection, *nsec );
            addRRSIG( response.mAuthoritySection, *nsec );

            Domainname wildcard = mApex;
            wildcard.addSubdomain( "*" );
            RRSetPtr wildcard_nsec = generateNSECRRSet( wildcard );
            addRRSet( response.mAuthoritySection, *wildcard_nsec );
            addRRSIG( response.mAuthoritySection, *wildcard_nsec );
        }

        return response;
    }

    std::vector<std::shared_ptr<RecordDS>> SignedZoneImp::getDSRecords() const
    {
	return mSigner.getDSRecords();
    }

    SignedZoneImp::RRSetPtr SignedZoneImp::findRRSet( const Domainname &domainname, Type type ) const
    {
        auto node = findNode( domainname );
        if ( node )
            return node->find( type );
        return RRSetPtr();
    }

    SignedZoneImp::NodePtr SignedZoneImp::findNode( const Domainname &name ) const
    {
        auto node = mOwnerToNode.find( name );
        if ( node != mOwnerToNode.end() ) {
            return node->second;
        }
        return NodePtr();
    }

    void SignedZoneImp::addSOAToAuthoritySection( PacketInfo &response ) const
    {
        if ( ! mSOA || mSOA->count() != 1 )
            throw std::logic_error( "SOA record must be exist in zone" );

        addRRSet( response.mAuthoritySection, *mSOA );
        if ( response.isDNSSECOK() ) {
	    addRRSIG( response.mAuthoritySection, *mSOA );
        }

    }

    void SignedZoneImp::addRRSet( std::vector<ResourceRecord> &section, const RRSet &rrset ) const
    {
	for ( auto data_itr = rrset.begin() ; data_itr != rrset.end() ; data_itr++ ) {
	    ResourceRecord r;
	    r.mDomainname = rrset.getOwner();
	    r.mType       = rrset.getType();
	    r.mClass      = rrset.getClass();
	    r.mTTL        = rrset.getTTL();
	    r.mRData      = *data_itr;
	    section.push_back( r );
	}
    }

    void SignedZoneImp::verify() const
    {
        if ( mSOA.get() == nullptr )
            throw ZoneError( "No SOA record" );

        if ( mNameServers.get() == nullptr )
            throw ZoneError( "No NS records" );
    }


    void SignedZoneImp::addRRSIG( std::vector<ResourceRecord> &section, const RRSet &original_rrset ) const
    {
	std::shared_ptr<RRSet> rrsigs = mSigner.signRRSet( original_rrset );
	
	for( auto rrsig : rrsigs->getRRSet() ) {
	    ResourceRecord r;
	    r.mDomainname = rrsigs->getOwner();
	    r.mType       = rrsigs->getType();
	    r.mClass      = rrsigs->getClass();
	    r.mTTL        = rrsigs->getTTL();
	    r.mRData      = rrsig;
	    section.push_back( r );
        }
    }

    SignedZoneImp::RRSetPtr SignedZoneImp::generateNSECRRSet( const Domainname &domainname ) const
    {
        OwnerToNodeContainer owner_to_node;
        for ( auto node : mOwnerToNode ) {
            if ( node.second->exist() )
                owner_to_node.insert( node );
        }

        OwnerToNodeContainer::const_iterator nsec_node = owner_to_node.end();
        Domainname next_domainname;

        auto node_itr = owner_to_node.find( domainname );
        if ( node_itr == owner_to_node.end() ) { // NXDOMAIN or NODATA by empty non-terminal
            for ( auto node_itr = owner_to_node.begin() ; node_itr != owner_to_node.end() ; node_itr++ ) {
                auto next_node_itr = std::next( node_itr );
                if ( next_node_itr != owner_to_node.end() ) {
                    if ( node_itr->first < domainname &&
                         domainname < next_node_itr->first ) {
                             nsec_node = node_itr;
                            next_domainname = next_node_itr->first;
                        }
                }
                else {
                    if ( domainname < owner_to_node.begin()->first || prev( owner_to_node.end() )->first < domainname ) {
                        nsec_node = prev( owner_to_node.end() );
                        next_domainname = owner_to_node.begin()->first;
                    }
                }
            }
            if ( nsec_node == owner_to_node.end() ) {
                throw std::logic_error( "not found nsec node for NXDOMAIN" );
            }
        }
        else { // NODATA
            nsec_node = node_itr;
            auto next_node_itr = std::next( nsec_node );
            if ( next_node_itr == owner_to_node.end() ) {
                next_domainname = owner_to_node.begin()->first;
            }
            else {
                next_domainname = next_node_itr->first;
            }
        }

        std::vector<Type> types;
        for ( auto rrset_itr = nsec_node->second->begin() ; rrset_itr != nsec_node->second->end() ; rrset_itr++ ) {
            types.push_back( rrset_itr->second->getType() );
        }
        std::shared_ptr<const RecordSOA> soa = std::dynamic_pointer_cast<const RecordSOA>( (*mSOA)[0] );
        std::shared_ptr<RRSet> nsec( new RRSet( nsec_node->first, mSOA->getClass(), TYPE_NSEC, soa->getMinimum() ) );
        nsec->add( std::shared_ptr<RecordNSEC>( new RecordNSEC( next_domainname, types ) ) );

        return nsec;
    }

    std::shared_ptr<RRSet> SignedZoneImp::signRRSet( const RRSet &rrset ) const
    {
        return mSigner.signRRSet( rrset );
    }

    void SignedZoneImp::initialize()
    {
        ZoneSigner::initialize();
    }
}

