/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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

enum class PersonaSwitchMode {
    /** see man setuid/setgid
     *  one-way switch, cannot go back
     */
    setRealId
    /** see man seteuid/setegid
     *  can go back to previous user
     */
    , setEffectiveId

    /** see man setreuid/setreguid (keeps real ids)
     * can go back to previous user
     * sets saved set-user-ID as well -> can be signalled by new persona
     */
    , setEffectiveAndSavedId
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
