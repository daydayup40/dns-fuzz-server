#include "zone.hpp"
#include <algorithm>
#include <sstream>
#include <iterator>

namespace dns
{
    std::string RRSet::toString() const
    {
        std::ostringstream os;

        os << getOwner().toString() << " "
           << getTTL() << " "
           << typeCodeToString( getType() ) << std::endl;

        for ( auto rr : mResourceData )
            os << "  " << rr->toString() << std::endl;

        return os.str();
    }

    std::ostream &operator<<( std::ostream &os, const RRSet &rrset )
    {
        os << rrset.toString();
        return os;
    }


    Node::RRSetPtr Node::find( Type t ) const
    {
        auto rrset_itr = mRRSets.find( t );
        if ( rrset_itr == mRRSets.end() )
            return RRSetPtr();
        return rrset_itr->second;
    }
}
