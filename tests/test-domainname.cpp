#include "dns.hpp"
#include "gtest/gtest.h"
#include <cstring>
#include <iostream>

// The fixture for testing class Foo.
class DomainnameTest : public ::testing::Test
{

public:
    virtual void SetUp()
    {
    }

    virtual void TearDown()
    {
    }
};

TEST_F( DomainnameTest, subdomain )
{
    dns::Domainname parent( "example.com" );
    dns::Domainname child( "child.example.com" );

    EXPECT_TRUE( parent.isSubDomain( child ) ) << "child.example.com is subdomain of example.com";
}

TEST_F( DomainnameTest, case_insenstive )
{
    dns::Domainname parent( "example.com" );
    dns::Domainname child( "child.EXAMPLE.com" );

    EXPECT_TRUE( parent.isSubDomain( child ) ) << "child.EXAMPLE.com is subdomain of example.com";
}

TEST_F( DomainnameTest, paranet_is_not_subdomain )
{
    dns::Domainname parent( "example.com" );
    dns::Domainname child( "child.example.jp" );

    EXPECT_FALSE( parent.isSubDomain( child ) ) << "child.example.jp is not subdomain of example.com";
}

TEST_F( DomainnameTest, not_subdomain )
{
    dns::Domainname parent( "child.example.com" );
    dns::Domainname child( "example.com" );

    EXPECT_FALSE( parent.isSubDomain( child ) ) << "example.com is not subdomain of child.example.com";
}

TEST_F( DomainnameTest, same_domainname )
{
    dns::Domainname parent( "example.com" );
    dns::Domainname child( "example.com" );

    EXPECT_TRUE( parent.isSubDomain( child ) ) << "example.com is subdomain of example.com";
}

int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
