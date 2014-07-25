#ifndef shared_service_persona_hpp_included_
#define shared_service_persona_hpp_included_

#include <boost/optional.hpp>

#include "utility/identity.hpp"

namespace service {

struct Persona {
    utility::Identity start;
    utility::Identity running;
};

} // namespace service

#endif // shared_service_persona_hpp_included_
