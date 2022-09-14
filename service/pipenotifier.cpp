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

#include <system_error>

#include <unistd.h>

#include <dbglog/dbglog.hpp>

#include "pipenotifier.hpp"

namespace service {

PipeNotifier::PipeNotifier(utility::Runnable &runnable)
    : runnable_(runnable), fd_{ -1, -1 }
{
    // direct I/O -> writes should send whole packets
    if (-1 == ::pipe2(fd_, O_DIRECT)) {
        std::system_error e(errno, std::system_category());
        LOG(err2) << "Failed to create notifier pipe: <" << e.what() << ">.";
        throw e;
    }
}

PipeNotifier::~PipeNotifier()
{
    for (auto fd : fd_) {
        if (fd >= 0) { ::close(fd); }
    }
}

std::string PipeNotifier::master()
{
    char buffer[PIPE_BUF];
    for (;;) {
        auto r(::read(fd_[0], buffer, sizeof(buffer)));
        if (-1 == r) {
            if (EINTR == errno) {
                if (!runnable_.isRunning()) {
                    LOGTHROW(err2, std::runtime_error)
                        << "Interrupted while writing to notification pipe.";
                }
                continue;
            } else {
                std::system_error e(errno, std::system_category());
                LOG(err2) << "Error writing to notification pipe: <"
                          << e.what() << ">.";
                throw e;
            }
        }

        return std::string(buffer, r);
    }
}

void PipeNotifier::slave(const std::string &string)
{
    if (string.size() > PIPE_BUF) {
        LOGTHROW(err2, std::runtime_error)
            << "Notification string too large (" << string.size()
            << " > PIPE_BUF(" << PIPE_BUF << ").";
    }

    for (;;) {
        auto w(::write(fd_[1], string.data(), string.size()));
        if (-1 == w) {
            if (EINTR == errno) {
                if (!runnable_.isRunning()) {
                    LOGTHROW(err2, std::runtime_error)
                        << "Interrupted while writing to notification pipe.";
                }
                continue;
            } else {
                std::system_error e(errno, std::system_category());
                LOG(err2) << "Error writing to notification pipe: <"
                          << e.what() << ">.";
                throw e;
            }
        }

        break;
    }
}

} // namespace service
