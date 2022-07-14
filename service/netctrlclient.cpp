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

#include <boost/asio.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "dbglog/dbglog.hpp"

#include "utility/tcpendpoint-io.hpp"
#include "utility/md5.hpp"
#include "utility/uri.hpp"
#include "utility/format.hpp"

#include "ctrlhandshake.hpp"
#include "netctrlclient.hpp"
#include "detail/ctrlclient.hpp"

namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace asio = boost::asio;

using tcp = asio::ip::tcp;

namespace service {

constexpr const int NetCtrlClient::DefaultPort;

struct NetCtrlClient::Detail {
    Detail(const utility::TcpEndpoint &endpoint
           , const std::string &component
           , const std::string &secret)
        : endpoint(endpoint)
        , ctrl(boost::lexical_cast<std::string>(endpoint), endpoint.value
               , component)
        , component(component), secret(secret)
    {
        const auto challenge(command(component).front());
        command(ctrlResponse(challenge, secret));
    }

    CtrlClientBase::Result command(const std::string &command) {
        return ctrl.command(command);
    }

    utility::TcpEndpoint endpoint;
    detail::CtrlClient<tcp> ctrl;
    const std::string component;
    const std::string secret;
};

NetCtrlClient::NetCtrlClient(const Params &params)
    : detail_(std::make_shared<Detail>
              (params.endpoint, params.component, params.secret))
{
}

NetCtrlClient::Params::Params(const std::string &uri)
{
    utility::Uri u(uri);
    if (!ba::iequals(u.scheme(), "ctrl")) {
        auto tmp(u);
        tmp.dropAuthInfo(true);
        LOGTHROW(err1, std::runtime_error)
            << "URI " << tmp.str() << " is not a ctrl URI.";
    }

    auto port(u.port());
    if (port < 0) { port = DefaultPort; }

    endpoint = {utility::format("%s:%s", u.host(), port)
                , utility::TcpEndpoint::ParseFlags::allowResolve };
    component = u.user();
    secret = u.password();
}

CtrlClientBase::Result NetCtrlClient::command(const std::string &command)
{
    return detail().command(command);
}

} // namespace service
