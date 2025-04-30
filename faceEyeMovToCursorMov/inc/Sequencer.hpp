/*
 * This is a C++ version of the canonical pthread service example. It intends
 * to abstract the service management functionality and sequencing for ease
 * of use. Much of the code is left to be implemented by the student.
 *
 * Build with g++ --std=c++23 -Wall -Werror -pedantic
 * Steve Rizor 3/16/2025
 */

#pragma once

#include <cstdint>
#include <iostream>
#include <functional>
#include <thread>
#include <vector>
#include <semaphore.h>
#include <atomic>





// The service class contains the service function and service parameters
// (priority, affinity, etc). It spawns a thread to run the service, configures
// the thread as required, and executes the service whenever it gets released.
class Service
{
public:
    uint32_t getPeriod() const { return _period; }
    std::string service_name; // Added to store service name

    template<typename T>
    Service(std::string name, T&& doService, uint8_t affinity, uint8_t priority, uint32_t period) :
        _doService(doService)
    {
        // store service configuration values
        service_name = std::move(name),
        _affinity = affinity;
        _priority = priority;
        _period = period;
        _isRunning = true;
        // initialize release semaphore
        sem_init(&_releaseSem, 0, 0); 
        // Start the service thread, which will begin running the given function immediately
        _service = std::jthread(&Service::_provideService, this);
    }
 
    void stop(){
        // change state to "not running" using an atomic variable
        _isRunning = false;
        sem_post(&_releaseSem);
        _service.request_stop(); 
        sem_destroy(&_releaseSem);

        // Log execution statistics
        logStatistics();


        // (heads up: what if the service is waiting on the semaphore when this happens?)
    }
 
    void release(){
        // release the service using the semaphore
 
        if(sem_post(&_releaseSem)!=0)
        {
            printf("Error %d\n",_period);
        }
    }
 
private:
    std::function<void(void)> _doService;
    std::jthread _service;
    sem_t _releaseSem;
    std::atomic<bool> _isRunning; // Changed to std::atomic<bool>


    uint8_t _affinity;
    uint8_t _priority;
    uint32_t _period;

    // Timing statistics
    std::chrono::high_resolution_clock::time_point _lastStartTime;
    double _minExecTime = std::numeric_limits<double>::max();
    double _maxExecTime = 0.0;
    double _totalExecTime = 0.0;
    int _executionCount = 0;

    double _minStartJitter = std::numeric_limits<double>::max();
    double _maxStartJitter = 0.0;


    void _initializeService()
    {
        // set affinity, priority, sched policy
        pthread_t thisThread = pthread_self();

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(_affinity, &cpuset);

        if (pthread_setaffinity_np(thisThread, sizeof(cpu_set_t), &cpuset) != 0) {
            perror("Failed to set CPU affinity");
            return;
        }


        sched_param sch_params;
        sch_params.sched_priority = _priority;

        if (pthread_setschedparam(thisThread, SCHED_FIFO, &sch_params) != 0) {
            perror("Failed to set scheduling policy/priority");
            return;
        }


        return;
        // (heads up: the thread is already running and we're in its context right now)
    }

    void _provideService()
    {
        _initializeService();
        // todo: call _doService() on releases (sem acquire) while the atomic running variable is true
        while (_isRunning) {
            sem_wait(&_releaseSem);

            

            if (_isRunning) {

                auto start = std::chrono::high_resolution_clock::now();

                // Calculate start time jitter
                if (_executionCount > 0)
                {
                    double actualInterval = std::chrono::duration<double, std::milli>(start - _lastStartTime).count();
                    double expectedInterval = _period;
                    double jitter = std::abs(actualInterval - expectedInterval);
    
                    _minStartJitter = std::min(_minStartJitter, jitter);
                    _maxStartJitter = std::max(_maxStartJitter, jitter);
                }
    
                _lastStartTime = start;

                _doService();

                auto end = std::chrono::high_resolution_clock::now();
                double execTime = std::chrono::duration<double, std::milli>(end - start).count();
    
                _minExecTime = std::min(_minExecTime, execTime);
                _maxExecTime = std::max(_maxExecTime, execTime);
                _totalExecTime += execTime;
                _executionCount++;
            }
        }
        
    }

    void logStatistics()
    {
        if (_executionCount == 0)
        {
            std::cout << "No execution data for service with period " << _period << "\n";
            return;
        }

        double avgExecTime = _totalExecTime / _executionCount;
        double execJitter = _maxExecTime - _minExecTime;
        double startJitter = _maxStartJitter - _minStartJitter;

        std::cout << "Service Stats (Period: " << _period << " ms):\n";
        std::cout << "Service Name " << service_name.c_str() << "\n";
        std::cout << "  Min Execution Time: " << _minExecTime << " ms\n";
        std::cout << "  Max Execution Time: " << _maxExecTime << " ms\n";
        std::cout << "  Avg Execution Time: " << avgExecTime << " ms\n";
        std::cout << "  Execution Time Jitter: " << execJitter << " ms\n";
        std::cout << "  Min Start Time Jitter: " << _minStartJitter << " ms\n";
        std::cout << "  Max Start Time Jitter: " << _maxStartJitter << " ms\n";
        std::cout << "  Start Time Jitter: " << startJitter << " ms\n";
    }
};
 





// The sequencer class contains the services set and manages
// starting/stopping the services. While the services are running,
// the sequencer releases each service at the requisite timepoint.

class Sequencer
{
public:
    template<typename... Args>
    void addService(Args&&... args)
    {
        _services.emplace_back(std::make_unique<Service>(std::forward<Args>(args)...));
        
    }

    void startServices()
    {
        for (auto& svc : _services) {
            timer_t timerId;

            struct sigevent sev{};
            sev.sigev_notify = SIGEV_THREAD;
            sev.sigev_value.sival_ptr = svc.get();
            sev.sigev_notify_function = [](union sigval val) {
                auto* s = static_cast<Service*>(val.sival_ptr);
                s->release();
            };

            if (timer_create(CLOCK_REALTIME, &sev, &timerId) != 0) {
                perror("Failed to create timer");
                continue;
            }

            // Set periodic interval based on svc.getPeriod()
            struct itimerspec its{};
            int period_ms = svc->getPeriod();

            its.it_value.tv_sec = period_ms / 1000;
            its.it_value.tv_nsec = (period_ms % 1000) * 1'000'000;
            its.it_interval = its.it_value; // make it periodic

            if (timer_settime(timerId, 0, &its, nullptr) != 0) {
                perror("Failed to start timer");
                timer_delete(timerId); // clean up if failed
                continue;
            }

            _timerIds.push_back(timerId);
        }
    }

    void stopServices()
    {
        // Stop all timers
        for (auto& timer : _timerIds) {
            timer_delete(timer);
        }
        _timerIds.clear();

        // Stop all services
        for (auto& svc : _services) {
            svc->stop();
        }
    }

private:
    std::vector<std::unique_ptr<Service>> _services;
    std::vector<timer_t> _timerIds;
};
