/**
 * Copyright (c) 2022 Melown Technologies SE
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

#ifndef service_netctrl_hpp_included_
#define service_netctrl_hpp_included_

#include <string>
#include <memory>
#include <stdexcept>

#include <boost/filesystem/path.hpp>

#include "utility/streams.hpp"
#include "utility/tcpendpoint.hpp"
#include "utility/ctrlcommand.hpp"

#include "ctrlclient.hpp"

namespace service {

/** Control socket client.
 *
 *  NB: synchronous version only
 */
class NetCtrlClient : public CtrlClientBase {
public:
    struct Params {
        utility::TcpEndpoint endpoint;
        std::string component;
        std::string secret;

        Params() = default;

        Params(const utility::TcpEndpoint &endpoint
               , const std::string &component
               , const std::string &secret)
            : endpoint(endpoint), component(component), secret(secret)
        {}

        // URI format: ctrl://component:secret&hostname:port/
        Params(const std::string &uri);
    };

    NetCtrlClient(const Params &params);

    /** Synchronously sends command and receives reply.
     */
    Result command(const std::string &command) override;

    struct Detail;

    static constexpr const int DefaultPort = 2020;

private:
    Detail& detail() { return *detail_; }
    const Detail& detail() const { return *detail_; }

    std::shared_ptr<Detail> detail_;
};

} // namespace service

#endif // service_netctrl_hpp_included_
