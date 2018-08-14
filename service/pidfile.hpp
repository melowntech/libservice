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
#ifndef shared_service_pidfile_hpp_included_
#define shared_service_pidfile_hpp_included_

#include <ctime>
#include <stdexcept>
#include <utility>

#include <boost/filesystem/path.hpp>

namespace service { namespace pidfile {

struct AlreadyRunning : std::runtime_error {
    AlreadyRunning(const std::string &msg) : std::runtime_error(msg) {}
};

/** Ensures this process is the only running instance of current program in
 *  the whole system.
 *
 * This function must be run in the main process AFTER daemonization. Otherwise
 * lock on this file is lost!
 */
void allocate(const boost::filesystem::path &path);

/** Returns true if server was signalled, false if it is not running.
 *  Throws std::exception if an error occurred.
 */
long signal(const boost::filesystem::path &path, int signal
            , bool reportMissingPid = false);

/** Scoped PID file: RAII helper.
 */
class ScopedPidFile {
public:
    /** Allocates PID file; fails when someone is already holding the file.
     *
     * \path path to PID file
     */
    ScopedPidFile(const boost::filesystem::path &path);

    /** Allocates PID file; waits for given time for pid file to become
     *  available.
     *
     * \path path to PID file
     * \path waitTime time to wait for PID allocation (in sec)
     * \path checkPeriod time between allocation attempts (in usec)
     */
    ScopedPidFile(const boost::filesystem::path &path
                  , std::time_t waitTime, long checkPeriod = 500000);

    /** Placeholder.
     */
    ScopedPidFile() = default;

    ScopedPidFile(ScopedPidFile &&o) { std::swap(path_, o.path_); }

    ~ScopedPidFile();

private:
    boost::filesystem::path path_;
};

} } // namespace service::pidfile

#endif // shared_service_pidfile_hpp_included_
