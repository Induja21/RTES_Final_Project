#pragma once

#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#include <semaphore>
#include <atomic>
#include <chrono>
#include <sched.h>
#include <pthread.h>
#include <cstdio>

#define INITIAL_CYCLE_STATS_IGNORE_COUNT (10) // Give time for execution to stabilze before tracking min and max values
#define RELEASE_MARGIN_NS (50) // Service can release up to this many nanoseconds early
#define MAX_SLEEP_MS (100L)
#define MS_TO_NS   (1000000L)
#define NS_PER_SEC (1000000000L)

static int _delta_t(struct timespec *stop, struct timespec *start, struct timespec *delta_t);
class Service {
public:
    uint8_t service_affinity;
    uint8_t service_priority;
    uint32_t service_period;

    template<typename T>
    Service(T&& doService, uint8_t affinity, uint8_t priority, uint32_t period) :
        service_affinity(affinity),
        service_priority(priority),
        service_period(period),
        _doService(std::forward<T>(doService)),
        _running(true),
        _sem(0)
    {
        _service = std::jthread(&Service::_provideService, this);
    }

    // Fixed move constructor
    Service(Service&& other) noexcept :
        service_affinity(other.service_affinity),
        service_priority(other.service_priority),
        service_period(other.service_period),
        _doService(std::move(other._doService)),
        _running(other._running.load(std::memory_order_relaxed)),
        _sem(0)
    {
        other.stop(); // Stop the thread in the moved-from object
        _service = std::jthread(&Service::_provideService, this); // Start a new thread
        while (other._sem.try_acquire()) {} // Ensure semaphore is safe
    }

    // Fixed move assignment operator
    Service& operator=(Service&& other) noexcept
    {
        if (this != &other)
        {
            stop(); // Stop the current thread
            service_affinity = other.service_affinity;
            service_priority = other.service_priority;
            service_period = other.service_period;
            _doService = std::move(other._doService);
            _running.store(other._running.load(std::memory_order_relaxed), std::memory_order_relaxed);
            _service = std::jthread(&Service::_provideService, this); // Start a new thread
            other.stop(); // Stop the thread in the moved-from object
        }
        return *this;
    }

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    void stop()
    {
        _running.store(false, std::memory_order_relaxed);
        _sem.release(); // Unblock the semaphore
        if (_service.joinable())
        {
            _service.join(); // Ensure the thread exits
        }
    }

    void release()
    {
        _sem.release();
    }
    void calcNextRelease(struct timespec *current_time, struct timespec *new_release_time)
    {
        if (!current_time || !new_release_time)
           return; // Null Pointer!
           
        // Calculate next expected release time
       new_release_time->tv_nsec = current_time->tv_nsec + (service_period * MS_TO_NS);  // Convert from milliseconds to nanoseconds
       new_release_time->tv_sec = current_time->tv_sec;
       if (new_release_time->tv_nsec > NS_PER_SEC) // If the nanoseconds exceed 1 second
       {
           new_release_time->tv_sec  += 1;
           new_release_time->tv_nsec -= NS_PER_SEC;
       }
    }
private:
    std::function<void(void)> _doService;
    std::jthread _service;
    std::atomic<bool> _running;
    std::counting_semaphore<1> _sem;

    void _initializeService()
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(service_affinity, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
        {
            std::puts("Failed to set thread affinity");
        }
        struct sched_param param;
        param.sched_priority = service_priority;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
        {
            std::puts("Failed to set thread priority");
        }
    }

    void _provideService()
    {
        _initializeService();
        
        // Timing Statistics Variables
        struct timespec start_time, stop_time, next_start, start_jitter, exec_jitter;
        unsigned long int exec_min_ns = NS_PER_SEC, exec_max_ns = 0, exec_time, iteration_count = 0;
        double exec_avg_ns = 0.0, start_avg_ns = 0.0;
        long int start_min_ns = 1000 * MS_TO_NS, start_max_ns = -1000 * MS_TO_NS, start_dev;
        
        clock_gettime(CLOCK_MONOTONIC, &next_start);
    
        // Call _doService() on releases (sem acquire) while the atomic running variable is true
        // Loop until service is stopped
        while (_running.load(std::memory_order_relaxed)) 
        {
            // Wait until the semaphore is released
            _sem.acquire();
    
            // If still running, execute the service function (for faster shutdown)
            if (_running.load(std::memory_order_relaxed)) 
            {
                // Get start timestamp
                clock_gettime(CLOCK_MONOTONIC, &start_time);
                
                _doService(); // Execute the service function
                
                // Get end timestamp
                clock_gettime(CLOCK_MONOTONIC, &stop_time);
                
                // Calculate updated timing statistics: execution time jitter (min/max/avg) & start time jitter (min/max/avg ??)
                // Calculate start and execution time
                iteration_count++;
                _delta_t(&start_time, &next_start, &start_jitter);
                start_dev = start_jitter.tv_nsec + (NS_PER_SEC * start_jitter.tv_sec);
                _delta_t(&stop_time, &start_time, &exec_jitter);
                exec_time = exec_jitter.tv_nsec + (NS_PER_SEC * exec_jitter.tv_sec);
                
                // Update statistics
                if(iteration_count > INITIAL_CYCLE_STATS_IGNORE_COUNT)
                {
                    if(exec_min_ns > exec_time)
                        exec_min_ns = exec_time;
                    if(exec_max_ns < exec_time)
                        exec_max_ns = exec_time;
                    
                    if(start_min_ns > start_dev)
                        start_min_ns = start_dev;
                    if(start_max_ns < start_dev)
                        start_max_ns = start_dev;
                }
                exec_avg_ns = (exec_avg_ns * (iteration_count - 1.0) / (double)iteration_count) + (exec_time / (double)iteration_count);
                start_avg_ns = (start_avg_ns * (iteration_count - 1.0) / (double)iteration_count) + (start_dev / (double)iteration_count);
                
                
                // Calculate next expected release time
                calcNextRelease(&start_time, &next_start);
            }
        }
        long jitter_ns = start_max_ns - start_min_ns;
        // Log calculated timing statistics
        std::printf(
            "\nExecution Time (in milliseconds) Stats for thread %lu with period %u ms\n"
            "Min: %.3f ms  \tMax: %.3f ms  \tAvg: %.3f ms\tJitter: %.3f ms\n"
            "Start Deviation (in milliseconds):\n"
            "Min: %.3f ms  \tMax: %.3f ms  \tAvg: %.3f ms\tJitter: %.3f ms\n"
            "Cycles Ran: %lu\n",
            (unsigned long)pthread_self(), 
            service_period, 
            exec_min_ns / 1000000.0, 
            exec_max_ns / 1000000.0, 
            exec_avg_ns / 1000000.0, 
            (exec_max_ns - exec_min_ns) / 1000000.0,
            start_min_ns / 1000000.0, 
            start_max_ns / 1000000.0, 
            start_avg_ns / 1000000.0,
            jitter_ns / 1000000.0,
            iteration_count
        );
        
        // Automatic Joining of Thread in destructor invoked here
    }
    
};


class Sequencer {
public:
    template<typename... Args>
    void addService(Args&&... args)
    {
        _services.emplace_back(std::forward<Args>(args)...);
    }

    void startServices()
    {
        _running.store(true, std::memory_order_relaxed);
        _timer = std::jthread(&Sequencer::_runTimer, this);
    }

    void stopServices()
    {
        _running.store(false, std::memory_order_relaxed);
        if (_timer.joinable())
        {
            _timer.join();
        }
        for (auto& service : _services)
        {
            service.stop();
        }
    }

private:
    std::vector<Service> _services;
    std::jthread _timer;
    std::atomic<bool> _running{false};
    void _runTimer()
    {
        // Create a vector of service indices, then sort it based on service_period
        std::vector<std::vector<Service>::iterator> sorted_service_iters;
        for (auto it = _services.begin(); it != _services.end(); ++it)
        {
            sorted_service_iters.push_back(it);
        }
        std::sort(sorted_service_iters.begin(), sorted_service_iters.end(),
                  [](std::vector<Service>::iterator a, std::vector<Service>::iterator b) {
                      return a->service_period < b->service_period; // Sort by period
                  });
    
        // Store next release times for each service
        std::vector<struct timespec> next_release_times(sorted_service_iters.size());
        struct timespec start_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    
        // Initial release for each service and set initial timeout times
        for (size_t i = 0; i < sorted_service_iters.size(); ++i)
        {
            sorted_service_iters[i]->release();
            next_release_times[i] = start_time;
            // Add service_period (converted from ms to timespec) to start_time
            long period_ns = sorted_service_iters[i]->service_period * MS_TO_NS;
            next_release_times[i].tv_sec += period_ns / NS_PER_SEC;
            next_release_times[i].tv_nsec += period_ns % NS_PER_SEC;
            if (next_release_times[i].tv_nsec >= NS_PER_SEC) {
                next_release_times[i].tv_nsec -= NS_PER_SEC;
                next_release_times[i].tv_sec += 1;
            }
        }
    
        std::vector<Service>::iterator next_release_service;
    
        // Main loop
        while (_running) // Assuming _running is a boolean, adjust if atomic
        {
            struct timespec current_time, sleep_time, time_to_release;
            time_to_release.tv_sec = 0;
            sleep_time.tv_nsec = MAX_SLEEP_MS * MS_TO_NS; // Default max sleep
            clock_gettime(CLOCK_MONOTONIC, &current_time);
    
            // Find the soonest next release time
            for (size_t i = 0; i < sorted_service_iters.size(); ++i)
            {
                auto& service = sorted_service_iters[i];
                auto& timeout = next_release_times[i];
    
                // Calculate time to next release
                time_to_release.tv_nsec = ((timeout.tv_sec - current_time.tv_sec) * NS_PER_SEC) +
                                          (timeout.tv_nsec - current_time.tv_nsec);
                if (time_to_release.tv_nsec < sleep_time.tv_nsec)
                {
                    sleep_time = time_to_release;
                    next_release_service = service;
                }
            }
    
            // Sleep until the next release time (with a minimum sleep to avoid lockups)
            if (sleep_time.tv_nsec < 10)
                sleep_time.tv_nsec = 10; // Minimum sleep time
            nanosleep(&sleep_time, nullptr);
    
            // Check and release the service if it's time
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            for (size_t i = 0; i < sorted_service_iters.size(); ++i)
            {
                auto& service = sorted_service_iters[i];
                auto& timeout = next_release_times[i];
                if ((current_time.tv_sec > timeout.tv_sec) ||
                    (current_time.tv_sec == timeout.tv_sec && current_time.tv_nsec >= timeout.tv_nsec))
                {
                    service->release();
                    // Calculate next release time
                    long period_ns = service->service_period * MS_TO_NS;
                    timeout.tv_sec += period_ns / NS_PER_SEC;
                    timeout.tv_nsec += period_ns % NS_PER_SEC;
                    if (timeout.tv_nsec >= NS_PER_SEC) {
                        timeout.tv_nsec -= NS_PER_SEC;
                        timeout.tv_sec += 1;
                    }
                }
            }
        }
    }
};

int _delta_t(struct timespec *stop, struct timespec *start, struct timespec *delta_t)
{ // Modified from provided Exercise 1 code
	long dt_sec=stop->tv_sec - start->tv_sec;
	long dt_nsec=stop->tv_nsec - start->tv_nsec;

	if(dt_nsec >= 0)
	{
	  delta_t->tv_sec=dt_sec;
	  delta_t->tv_nsec=dt_nsec;
	}
	else
	{
	  delta_t->tv_sec=dt_sec-1;
	  delta_t->tv_nsec=NS_PER_SEC+dt_nsec;
	}

	return(1);
}