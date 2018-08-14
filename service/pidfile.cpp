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
#include <cstdio>
#include <system_error>
#include <unistd.h> // usleep

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <boost/filesystem.hpp>

#include "dbglog/dbglog.hpp"

#include "pidfile.hpp"

namespace fs = boost::filesystem;

namespace service { namespace pidfile {

namespace {

struct File {
    File() : fd(-1), fp(nullptr) {}
    ~File() { if (fd > -1) { close(); } }

    FILE* fdopen(const char *mode) { return (fp = ::fdopen(fd, mode)); }

    void close() {
        if (fp) { std::fclose(fp); }
        else if (fd) { ::close(fd); }
        fp = nullptr;
        fd = 0;
    }

    void release() { fd = -1; fp = nullptr; }

    int fd;
    std::FILE *fp;
};

int lockFd(int fd)
{
    struct ::flock lock;

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    return ::fcntl(fd, F_SETLK, &lock);
}

} // namespace

void allocate(const fs::path &path)
{
    // make sure we have place where to write the pid file
    create_directories(path.parent_path());

    File file;

    file.fd = ::open(path.string().c_str(), O_RDWR | O_CREAT | O_EXCL
                     , S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

    if (file.fd == -1) {
        if (errno != EEXIST) {
            std::system_error e(errno, std::system_category());
            LOG(err3) << "Cannot open pid file " << path << ": <"
                      << e.code() << ", " << e.what() << ">.";
            throw e;
        }

        if ((file.fd = ::open(path.string().c_str(), O_RDWR)) < 0) {
            std::system_error e(errno, std::system_category());
            LOG(err3) << "Cannot open pid file " << path << ": <"
                      << e.code() << ", " << e.what() << ">.";
            throw e;
        }

        if (!file.fdopen("rw")) {
            std::system_error e(errno, std::system_category());
            LOG(err3) << "Cannot open pid file " << path << " for reading: <"
                      << e.code() << ", " << e.what() << ">.";
            throw e;
        }

        pid_t pid = -1;
        if ((std::fscanf(file.fp, "%d", &pid) != 1)
            || (pid == ::getpid())
            || !lockFd(file.fd))
        {
            LOG(info4) << "Removing stale pid file for pid <" << pid << "> ["
                       << path.string() << "].";

            if (-1 == ::unlink(path.string().c_str())) {
                std::system_error e(errno, std::system_category());
                LOG(err3) << "Cannot unlink pid file " << path << ": "
                          << e.code() << ", " << e.what() << ">.";
                throw e;
            }
        } else {
            LOGTHROW(err3, AlreadyRunning)
                << "Another instance is running with pid <" << pid << "> ["
                << path.string() << "].";

        }
        file.close();

        ::unlink(path.string().c_str());
        file.fd = ::open(path.string().c_str(), O_RDWR | O_CREAT | O_EXCL,
                  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

        if (file.fd == -1) {
            std::system_error e(errno, std::system_category());
            LOG(err3) << "Cannot open pid file " << path
                      << " the second time round: <"
                      << e.code() << ", " << e.what() << ">.";
            throw e;
        }
    }

    if (-1 == lockFd(file.fd)) {
        std::system_error e(errno, std::system_category());
        LOG(err3) << "Cannot lock pid file " << path << ": <"
                  << e.code() << ", " << e.what() << ">.";
        throw e;
    }

    if (!file.fdopen("w")) {
        std::system_error e(errno, std::system_category());
        LOG(err3) << "Oops, fdopen on pid file  " << path << " failed: <"
                  << e.code() << ", " << e.what() << ">.";
        throw e;
    }

    std::fprintf(file.fp, "%d\n", ::getpid());
    std::fflush(file.fp);

    // neither file stream nor fd are closed to keep the lock active
    ::fcntl(file.fd, F_SETFD, long(1));

    file.release();
}

long signal(const fs::path &path, int signal
            , bool reportMissingPid)
{
    File file;

    if ((file.fd = ::open(path.string().c_str(), O_RDONLY)) < 0) {
        if (errno != ENOENT) {
            std::system_error e(errno, std::system_category());
            LOG(err3) << "Cannot open pid file " << path << ": <"
                      << e.code() << ", " << e.what() << ">.";
            throw e;
        }

        return reportMissingPid ? -1 : 0;
    }

    if (!file.fdopen("r")) {
        std::system_error e(errno, std::system_category());
        LOG(err3) << "Cannot open pid file " << path << " for reading: <"
                  << e.code() << ", " << e.what() << ">.";
        throw e;
    }

    if (!lockFd(file.fd)) {
        // process is not running
        return 0;
    }

    pid_t pid = 0;
    if (std::fscanf(file.fp, "%d", &pid) != 1) {
        std::system_error e(errno, std::system_category());
        LOG(err3) << "Cannot parse pid " << path << " for reading: <"
                      << e.code() << ", " << e.what() << ">.";
        throw e;
    }

    if (-1 == ::kill(pid, signal)) {
        if (errno == ESRCH) {
            return 0;
        }
        std::system_error e(errno, std::system_category());
        LOG(err3) << "Cannot deliver signal to running instance: <"
                  << e.code() << ", " << e.what() << ">.";
        throw e;
    }

    return pid;
}


ScopedPidFile::ScopedPidFile(const boost::filesystem::path &path)
    : path_(fs::absolute(path))
{
    allocate(path_);
}

ScopedPidFile::ScopedPidFile(const boost::filesystem::path &path
                             , std::time_t waitTime, long checkPeriod)
    : path_(fs::absolute(path))
{
    const auto end(std::time(nullptr) + waitTime);

    for (;;) {
        try {
            allocate(path_);
            return;
        } catch (const service::pidfile::AlreadyRunning&) {
            if (std::time(nullptr) >= end) {
                // time out, rethrow
                throw;
            }

            // wait next round
            ::usleep(checkPeriod);
        }
    }
}

ScopedPidFile::~ScopedPidFile()
{
    if (!path_.empty() && (-1 == ::unlink(path_.string().c_str()))) {
        std::system_error e(errno, std::system_category());
        LOG(err3) << "Cannot unlink pid file " << path_ << ": "
                  << e.code() << ", " << e.what() << ">.";
    }
}

} } // namespace service::pidfile
