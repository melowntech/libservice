#ifndef shared_service_pidfile_hpp_included_
#define shared_service_pidfile_hpp_included_

#include <boost/filesystem/path.hpp>

namespace service { namespace pidfile {

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
bool signal(const boost::filesystem::path &path, int signal);

} } // namespace service::pidfile

#endif // shared_service_pidfile_hpp_included_
