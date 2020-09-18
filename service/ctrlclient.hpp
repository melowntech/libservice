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

#ifndef service_ctrl_hpp_included_
#define service_ctrl_hpp_included_

#include <string>
#include <memory>
#include <stdexcept>

#include <boost/filesystem/path.hpp>

#include "utility/streams.hpp"
#include "utility/ctrlcommand.hpp"

namespace service {

/** Control socket client.
 *
 *  NB: synchronous version only
 */
class CtrlClient {
public:
    CtrlClient(const boost::filesystem::path &ctrl
               , const std::string &name = std::string());

    /** Synchronously sends command and receives reply.
     */
    std::vector<std::string> command(const std::string &command);

    template <typename ...Args>
    std::vector<std::string> command(const std::string &command
                                     , Args &&...args);

    struct Detail;

    bool parseBoolean(const std::string &line) const;

private:
    Detail& detail() { return *detail_; }
    const Detail& detail() const { return *detail_; }

    std::shared_ptr<Detail> detail_;
};

// inlines

template <typename ...Args>
std::vector<std::string> CtrlClient::command(const std::string &command
                                             , Args &&...args)
{
    return this->command(utility::concatWithSeparator
                         (" ", command, std::forward<Args>(args)...));
}

} // namespace service

#endif // service_ctrl_hpp_included_
