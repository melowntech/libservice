#ifndef shared_service_persona_hpp_included_
#define shared_service_persona_hpp_included_

#include <boost/optional.hpp>

#include "dbglog/dbglog.hpp"

#include "utility/identity.hpp"
#include "utility/guarded-call.hpp"

namespace service {

struct Persona {
    /** Persona at the moment service has been started.
     */
    utility::Identity start;

    /** Persona service is running at.
     */
    utility::Identity running;

};

/** Run call with elevated rights (process has been started with) and switch
 *  back to normal rights.
 */
template <typename Call>
auto runElevated(const boost::optional<Persona> &persona, Call call)
    -> decltype(call());

// implementation

template <typename Call>
auto runElevated(const boost::optional<Persona> &persona, Call call)
    -> decltype(call())
{
    // if no persona -> run as is
    if (!persona) { return call(); }

    // elevate rights, run and return rights back again
    return utility::guardedCall([&persona]()
    {
        LOG(info1) << "Switching to persona: <" << persona->start << ">.";
        setEffectivePersona(persona->start);
    }, [&call]
    {
        call();
    }, [&persona]()
    {
        LOG(info1) << "Switching back to persona: <"
                   << persona->running << ">.";
        setEffectivePersona(persona->running);
    });
}

} // namespace service

#endif // shared_service_persona_hpp_included_
