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
#include <boost/algorithm/string/predicate.hpp>

#include "dbglog/dbglog.hpp"

#include "utility/raise.hpp"

#include "ctrlclient.hpp"
#include "detail/ctrlclient.hpp"

namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace asio = boost::asio;


using asio::local::stream_protocol;

namespace service {

struct CtrlClient::Detail {
    Detail(const fs::path &ctrl, const std::string &name)
        : ctrl(ctrl.string(), stream_protocol::endpoint(ctrl.string())
                , name.empty() ? std::string("client") : name)
    {}

    std::vector<std::string> command(const std::string &command) {
        return ctrl.command(command);
    }

    detail::CtrlClient<stream_protocol> ctrl;
};

CtrlClient::CtrlClient(const fs::path &ctrl, const std::string &name)
    : detail_(std::make_shared<Detail>(ctrl, name))
{}

std::vector<std::string> CtrlClient::command(const std::string &command)
{
    return detail().command(command);
}

bool CtrlClientBase::parseBoolean(const std::string &line) const
{
    if (line == "true") {
        return true;
    } else if (line == "false") {
        return false;
    }

    LOGTHROW(err2, std::runtime_error)
        << "Invalid reply from server: <" << line << ">.";
    throw;
}

} // namespace service
