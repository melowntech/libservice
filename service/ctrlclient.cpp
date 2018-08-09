/**
 * Copyright (c) 2018 Melown Technologies SE
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
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include "dbglog/dbglog.hpp"

#include "./ctrlclient.hpp"

namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace asio = boost::asio;


using asio::local::stream_protocol;

namespace service {

struct CtrlClient::Detail {
    Detail(const fs::path &ctrl)
        : ctrl(ctrl), socket(ios)
    {
        socket.connect(stream_protocol::endpoint(ctrl.string()));
    }

    std::vector<std::string> command(const std::string &command);

    const fs::path ctrl;

    asio::io_service ios;
    stream_protocol::socket socket;
    asio::streambuf responseBuffer;
};

std::vector<std::string>
CtrlClient::Detail::command(const std::string &command)
{
    // command + newline
    std::vector<asio::const_buffer> request;
    request.emplace_back(command.data(), command.size());
    request.emplace_back("\n", 1);

    asio::write(socket, request);

    asio::read_until(socket, responseBuffer, "\4");

    std::istream is(&responseBuffer);

    std::string response;
    std::getline(is, response, '\4');

    std::vector<std::string> lines;
    return ba::split(lines, response, ba::is_any_of("\n"));
}

CtrlClient::CtrlClient(const fs::path &ctrl)
    : detail_(std::make_shared<Detail>(ctrl))
{}

std::vector<std::string> CtrlClient::command(const std::string &command)
{
    return detail().command(command);
}

} // namespace service