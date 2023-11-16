//
//  MUXException.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef MUXException_hpp
#define MUXException_hpp

#include <libgeneral/exception.hpp>

namespace tihmstar {

class MUXException : public tihmstar::exception {
public:
    using tihmstar::exception::exception;
};

#pragma mark custom catch exceptions
class MUXException_client_disconnected : public MUXException{
    using MUXException::MUXException;
};

};

#endif /* MUXException_hpp */
