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
#ifndef shared_service_service_hpp_included_
#define shared_service_service_hpp_included_

#include <memory>

#include <boost/optional.hpp>

#include "utility/runnable.hpp"
#include "utility/identity.hpp"
#include "utility/ctrlcommand.hpp"

#include "program.hpp"
#include "persona.hpp"

namespace service {

namespace detail {
    class SignalHandler;
} // namespace detail

class Service : protected Program, public utility::Runnable {
public:
    Service(const std::string &name, const std::string &version
            , int flags = 0x0);

    ~Service();

    int operator()(int argc, char *argv[]);

    bool isRunning() override;

    void stop() override;

    /** Adds/removes this process to list of processes that are mark global
     *  terminate flag on terminate signal. All other processes handle terminate
     *  signal locally.
     */
    void globalTerminate(bool value = true, long pid = 0);

    struct Config {
        std::string username;
        std::string groupname;
        bool loginEnv = false;

        Config() {}

        void configuration(po::options_description &cmdline
                           , po::options_description &config);

        void configure(const po::variables_map &vars);
    };

    typedef utility::CtrlCommand CtrlCommand;

    void processCtrl(const CtrlCommand &cmd, std::ostream &output);

    void processStat();

    void processMonitor(std::ostream &output);

    boost::optional<Persona> getPersona() const { return persona_; }

    /** Register signal norification.
     */
    void registerSignal(int signo);

    /** Returns whether we are configured to run as a daemon.
     */
    bool daemonize() { return daemonize_; }

protected:
    friend class detail::SignalHandler;

    typedef std::shared_ptr<void> Cleanup;

    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd) override = 0;

    virtual void configure(const po::variables_map &vars) override = 0;

    void configure(const std::vector<std::string> &unrecognized) override;

    /** Returned Cleanup will be destroyed when going down. Just return pointer
     *  to your cleanup code
     */
    virtual Cleanup start() = 0;

    virtual int run() = 0;

    bool help(std::ostream &out, const std::string &what) const override;

    void preConfigHook(const po::variables_map &vars) override;

    /** Code that will be run under original persona before persona is switched.
     *
     *  Returns flag whether original persona should be regainable.
     */
    virtual PersonaSwitchMode prePersonaSwitch() {
        return PersonaSwitchMode::setRealId;
    }

    /** Code that will be run under new persona after persona is switched.
     */
    virtual void postPersonaSwitch() {}

    /** Called before (possible) daemonization.
     *
     *  \param daemonize true when program is about to daemonize
     */
    virtual void preDaemonize(bool daemonize) { (void) daemonize; }

    virtual bool ctrl(const CtrlCommand &cmd, std::ostream &output);

    virtual void stat(std::ostream &output);

    virtual void monitor(std::ostream &output);

    /** Called on logrotate after log has been rotated.
     *
     * \param logFile path to current log file
     */
    virtual void logRotated(const boost::filesystem::path &logFile);

    /** Called when a user-registered signal occurs.
     */
    virtual void signal(int signo);

private:
    /** Called on log rotate event to re-open log file.
     *  Makes call to logRotated().
     */
    void logRotate();

    bool daemonize_;

    boost::optional<Persona> persona_;

    std::shared_ptr<detail::SignalHandler> signalHandler_;
};

} // namespace service

#endif // shared_service_service_hpp_included_
