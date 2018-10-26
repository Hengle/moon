/****************************************************************************

Git <https://github.com/sniper00/MoonNetLua>
E-Mail <hanyongtao@live.com>
Copyright (c) 2015-2017 moon
Licensed under the MIT License <http://opensource.org/licenses/MIT>.

****************************************************************************/

#pragma once
#include "config.h"
#include "asio.hpp"
#include "common/concurrent_queue.hpp"
#include "common/spinlock.hpp"

namespace moon
{
    class router;

    class worker
    {
    public:
        static const uint16_t MAX_SERVICE_NUM = 0xFFFF;

        friend class server;

        explicit worker(router* r);

        ~worker();

        worker(const worker&) = delete;

        worker& operator=(const worker&) = delete;

        void remove_service(uint32_t serviceid, uint32_t sender, uint32_t respid, bool crashed = false);

        asio::io_service& io_service();

        int32_t workerid() const;

        uint32_t servicenum() const;

        uint32_t make_serviceid();

        void add_service(const service_ptr_t& s);

        void send(const message_ptr_t& msg);

        void workerid(int32_t id);

        void shared(bool v);

        bool shared() const;

        service* find_service(uint32_t serviceid) const;

        void runcmd(uint32_t sender, const std::string& cmd, int32_t responseid);
    private:
        void run();

        void stop();

        void wait();

        bool stoped();

        template<typename THandler>
        void post(THandler&& h)
        {
            asio::post(io_ctx_, std::forward<THandler>(h));
        }
    private:
        void start();

        void update();

        void handle_one(service* ser, const message_ptr_t& msg);

        void register_commands();
    private:
        //To prevent post too many update event
        std::atomic_flag update_state_ = ATOMIC_FLAG_INIT;
        std::atomic<state> state_;
        std::atomic_bool shared_;
        int32_t workerid_;
        std::atomic<uint16_t> serviceuid_;
        std::atomic<uint32_t> servicenum_;

        int64_t work_time_;
        router*  router_;
        std::thread thread_;
        asio::io_context io_ctx_;
        asio::executor_work_guard<asio::io_context::executor_type> work_;
        std::unordered_map<uint32_t, service_ptr_t> services_;

        using queue_t = concurrent_queue<message_ptr_t, moon::spin_lock, std::vector>;
        queue_t::container_type swapqueue_;
        queue_t mqueue_;

        using command_hander_t = std::function<std::string(const std::vector<std::string>&)>;
        std::unordered_map<std::string, command_hander_t> commands_;
    };
};


