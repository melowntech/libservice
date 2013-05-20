#ifndef shared_service_runnable_hpp_included_
#define shared_service_runnable_hpp_included_

#include <boost/noncopyable.hpp>

namespace service {

class runable : boost::noncopyable {
public:
    virtual ~runable();
    virtual bool isRunning() = 0;
    virtual void stop() = 0;
};

} // namespace service

#endif // shared_service_runnable_hpp_included_
