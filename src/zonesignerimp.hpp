#ifndef ZONE_SIGNER_IMP_HPP
#define ZONE_SIGNER_IMP_HPP

#include "zonesigner.hpp"
#include <boost/utility.hpp>
#include <string>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <yaml-cpp/yaml.h>

namespace dns
{
    SignAlgorithm stringToSignAlgorithm( const std::string &str );
    const EVP_MD *enumToSignMD( SignAlgorithm algo );
    const EVP_MD *enumToDigestMD( HashAlgorithm algo );
    void throwException( const char *message, const char *other = nullptr );
    void throwException( const std::string &message, const char *other = nullptr );

    /*******************************************************************************************
     * PrivateKeyImp
     *******************************************************************************************/
    /*
      type: KSK
      domain: example.com
      algorithm: RSASHA1
      not_before: 1502238577
      not_after: 1502413242
      key_file: /etc/nsd/keys/example.com.ksk.key
    */
    class PrivateKeyImp : private boost::noncopyable
    {
    public:
        PrivateKeyImp( KeyType key_type,
		       SignAlgorithm algo,
		       EVP_PKEY *key,
		       uint32_t not_before,
		       uint32_t not_after,
		       const Domainname &domain )
            : mKeyType( key_type ),
	      mSignAlgorithm( algo ),
	      mPrivateKey( key ),
	      mNotBefore( not_before ),
	      mNotAfter( not_after ),
	      mDomainname( domain )
        {}

        ~PrivateKeyImp();

        KeyType           getKeyType() const    { return mKeyType; }
        SignAlgorithm     getAlgorithm() const  { return mSignAlgorithm; }
        EVP_PKEY         *getPrivateKey() const { return mPrivateKey; }
        uint32_t          getNotBefore() const  { return mNotBefore; }
        uint32_t          getNotAfter() const   { return mNotAfter; }
        const Domainname  getDomainname() const { return mDomainname; }
        uint16_t          getKeyTag() const; 
        std::shared_ptr<RecordDNSKEY> getDNSKEYRecord() const;
        
	virtual std::shared_ptr<PublicKey> getPublicKey() const = 0;
	virtual void sign( EVP_MD_CTX *mMDContext, const WireFormat &message, PacketData &signature ) const = 0;

	static std::vector<std::shared_ptr<PrivateKeyImp> > load( const std::string &config );
        static std::vector<std::shared_ptr<PrivateKeyImp> > loadConfig( const std::string &config_file );

    private:
        KeyType        mKeyType;
	SignAlgorithm  mSignAlgorithm;
        EVP_PKEY      *mPrivateKey;
        uint32_t       mNotBefore;
        uint32_t       mNotAfter;
        Domainname     mDomainname;

        static EVP_PKEY *loadPrivateKey( const std::string &key_file );

        template<typename TYPE>
        static TYPE loadParameter( const YAML::Node &node, const std::string param_name )
        {
            if ( node[param_name] )
                return node[param_name].as<TYPE>();
            throw std::runtime_error( param_name + " must be specified" ); 
        }

    };


    /*******************************************************************************************
     * RSAAPrivateKeyImp
     *******************************************************************************************/
    class RSAPrivateKeyImp : public PrivateKeyImp 
    {
    public:
	RSAPrivateKeyImp( KeyType key_type,
                          EVP_PKEY *key,
                          uint32_t not_before,
                          uint32_t not_after,
                          const Domainname &domain,
                          int hash_algo )
            : PrivateKeyImp( key_type,
			     DNSSEC_RSASHA1,
			     key,
			     not_before,
			     not_after,
			     domain ),
              mHashAlgorithm( hash_algo )
        {}

    	virtual std::shared_ptr<PublicKey> getPublicKey() const;
	virtual void sign( EVP_MD_CTX *mMDContext, const WireFormat &message, PacketData &signature ) const;

    private:
        int mHashAlgorithm;
    };


    /*******************************************************************************************
     * ECDSAPrivateKeyImp
     ***********************************************************************************/
    template<uint8_t SIGN_ALGO>
    class ECDSAPrivateKeyImp : public PrivateKeyImp 
    {
    public:
	ECDSAPrivateKeyImp( KeyType           key_type,
                            EVP_PKEY         *key,
                            uint32_t          not_before,
                            uint32_t          not_after,
                            const Domainname &domain )
            : PrivateKeyImp( key_type,
                             static_cast<SignAlgorithm>(SIGN_ALGO),
			     key,
			     not_before,
			     not_after,
			     domain )
        {}

	virtual std::shared_ptr<PublicKey> getPublicKey() const;
	virtual void sign( EVP_MD_CTX *mMDContext, const WireFormat &message, PacketData &signature ) const;
    };

    typedef ECDSAPrivateKeyImp<DNSSEC_ECDSAP256SHA256> ECDSAP256SHA256PrivateKeyImp;
    typedef ECDSAPrivateKeyImp<DNSSEC_ECDSAP384SHA384> ECDSAP384SHA384PrivateKeyImp;

    /*******************************************************************************************
     * ZoneSingerImp
     *******************************************************************************************/

    /*!
     * ZoneSingerImp
     */
    class ZoneSignerImp
    {
    private:
        Domainname  mApex;
        EVP_MD_CTX *mMDContext;
	std::vector< std::shared_ptr<PrivateKeyImp> > mKSKs;
        std::vector< std::shared_ptr<PrivateKeyImp> > mZSKs;

	std::shared_ptr<RecordRRSIG>  generateRRSIG( const RRSet &, const PrivateKeyImp &key ) const;
	std::shared_ptr<RRSet>        signRRSetByKeys( const RRSet &, const std::vector<std::shared_ptr<PrivateKeyImp> > &keys ) const;
        std::shared_ptr<RecordDS>     getDSRecord( const PrivateKeyImp &ksk, HashAlgorithm algo ) const;

    public:
        ZoneSignerImp( const Domainname &d, const std::string &ksks, const std::string &zsks );
        ~ZoneSignerImp();

	void sign( const WireFormat &message,
		   PacketData &signature,
		   const PrivateKeyImp &key ) const;

	std::vector<std::shared_ptr<PublicKey>> getKSKPublicKeys() const;
	std::vector<std::shared_ptr<PublicKey>> getZSKPublicKeys() const;

        std::vector<std::shared_ptr<RecordDS> >     getDSRecords() const;
        std::vector<std::shared_ptr<RecordDNSKEY> > getDNSKEYRecords() const;

	void generateSignData( const RRSet &rrset, const PrivateKeyImp &key, WireFormat &sign_target ) const;
	std::shared_ptr<RRSet> signRRSet( const RRSet & ) const;
	std::shared_ptr<RRSet> signDNSKEY( TTL ttl ) const;

        static void initialize();
    };

    /*******************************************************************************************
     * RSAPublicKeyImp
     *******************************************************************************************/
    /*!
     * RSAPublicKeyImp
     */
    class RSAPublicKeyImp
    {
    public:
        RSAPublicKeyImp( const PacketData &exp, const PacketData &mod );

        std::string toString() const;
        const PacketData &getExponent() const { return exponent; } 
        const PacketData &getModulus() const  { return modulus; }  
        PacketData getDNSKEYFormat() const;
    private:
	PacketData exponent;
	PacketData modulus;
    };

    /*******************************************************************************************
     * ECDSAPublicKeyImp
     *******************************************************************************************/
    /*!
     * ECDSAPublicKeyImp
     */
    class ECDSAPublicKeyImp
    {
    public:
        ECDSAPublicKeyImp( const PacketData &public_key );
        ECDSAPublicKeyImp( const uint8_t *p, ssize_t size );

        std::string toString() const;
        PacketData getDNSKEYFormat() const;
    private:
	PacketData mQ;
    };

}

#endif

