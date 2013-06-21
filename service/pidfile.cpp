#include <cstdio>
#include <system_error>

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
    ~File() { close(); }

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
            LOG(info4) << "Removing stale pid file for pid <" << pid << ">.";

            if (-1 == ::unlink(path.string().c_str())) {
                std::system_error e(errno, std::system_category());
                LOG(err3) << "Cannot unlink pid file " << path << ": "
                          << e.code() << ", " << e.what() << ">.";
                throw e;
            }
        } else {
            LOGTHROW(err3, std::runtime_error)
                << "Another instance is running with pid <" << pid << ">.";
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

unsigned long signal(const fs::path &path, int signal)
{
    File file;

    if ((file.fd = ::open(path.string().c_str(), O_RDONLY)) < 0) {
        if (errno != ENOENT) {
            std::system_error e(errno, std::system_category());
            LOG(err3) << "Cannot open pid file " << path << ": <"
                      << e.code() << ", " << e.what() << ">.";
            throw e;
        }

        return 0;
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

} } // namespace service::pidfile
