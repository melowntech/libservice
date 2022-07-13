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

#ifndef service_detail_ctrlclient_hpp_included_
#define service_detail_ctrlclient_hpp_included_

#include <string>
#include <memory>
#include <stdexcept>

#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "dbglog/dbglog.hpp"

#include "utility/streams.hpp"
#include "utility/ctrlcommand.hpp"
#include "utility/raise.hpp"

namespace service { namespace detail {

template <typename Protocol>
struct CtrlClient {
    using Endpoint = typename Protocol::endpoint;
    using Socket = typename Protocol::socket;

    CtrlClient(const std::string &endpointStr, const Endpoint &endpoint
               , const std::string &name)
        : endpointStr(endpointStr), endpoint(endpoint)
        , name(name.empty() ? std::string("client") : name)
        , socket(ios)
    {
        try {
            socket.connect(endpoint);
        } catch (const std::exception &e) {
            LOGTHROW(err2, std::runtime_error)
                << "Unable to connect to " << endpointStr << ": <" << e.what()
                << ">; is the server running?";
        }
    }

    std::vector<std::string> command(const std::string &command);

    const std::string endpointStr;
    const Endpoint endpoint;
    const std::string name;

    boost::asio::io_service ios;
    Socket socket;
    boost::asio::streambuf responseBuffer;
};

template <typename Protocol>
std::vector<std::string>
CtrlClient<Protocol>::command(const std::string &command)
{
    // command + newline
    std::vector<boost::asio::const_buffer> request;
    request.emplace_back(command.data(), command.size());
    request.emplace_back("\n", 1);

    boost::asio::write(socket, request);

    boost::asio::read_until(socket, responseBuffer, "\4");

    std::istream is(&responseBuffer);

    std::string response;
    std::getline(is, response, '\4');

    std::vector<std::string> lines;
    boost::algorithm::split(lines, response
                            , boost::algorithm::is_any_of("\n"));

    if (!lines.empty()) {
        if (boost::algorithm::starts_with(lines.front(), "error: ")) {
            utility::raise<utility::CtrlCommandError>
                ("%s: %s", name, lines.front().substr(7));
        }
        if (lines.back().empty()) { lines.pop_back(); }
    }

    return lines;
}

} } // namespace service::detail

#endif // service_detail_ctrlclient_hpp_included_
