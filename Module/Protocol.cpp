/*
* Copyright (c) 2022 DWVoid and Infinideastudio Team
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include "kls/phttp/Error.h"
#include "kls/phttp/Protocol.h"
#include "kls/coroutine/Mutex.h"
#include "kls/coroutine/Future.h"
#include "kls/coroutine/Operation.h"
#include <unordered_map>

using namespace kls::io;
using namespace kls::phttp;
using namespace kls::thread;
using namespace kls::essential;
using namespace kls::coroutine;

namespace {
    struct Message {
        int stage{0};
        kls::phttp::Block blocks[3];
    };

    Message pack(Request r, int32_t id, kls::pmr::MemoryResource *memory) {
        r.body.set_id(id);
        return Message{.blocks = {r.line.pack(id, memory), r.headers.pack(id, memory), std::move(r.body)}};
    }

    Message pack(Response r, int32_t id, kls::pmr::MemoryResource *memory) {
        r.body.set_id(id);
        return Message{.blocks = {r.line.pack(id, memory), r.headers.pack(id, memory), std::move(r.body)}};
    }

    Request unpack_request(Message m, kls::pmr::MemoryResource *memory) {
        return Request{
                .line = RequestLine::unpack(m.blocks[0], memory),
                .headers = Headers::unpack(m.blocks[1], memory),
                .body = std::move(m.blocks[2])
        };
    }

    Response unpack_response(Message m, kls::pmr::MemoryResource *memory) {
        return Response{
                .line = ResponseLine::unpack(m.blocks[0], memory),
                .headers = Headers::unpack(m.blocks[1], memory),
                .body = std::move(m.blocks[2])
        };
    }

    ValueAsync<> locked_send_message(Endpoint &endpoint, Message message, Mutex &mutex) {
        MutexLock lk = co_await mutex.scoped_lock_async();
        co_await endpoint.put(std::move(message.blocks[0]));
        co_await endpoint.put(std::move(message.blocks[1]));
        co_await endpoint.put(std::move(message.blocks[2]));
    }

    ValueAsync<> post_shutdown_user(Endpoint &endpoint, Mutex &mutex) {
        MutexLock lk = co_await mutex.scoped_lock_async();
        auto message = Block(0, -1, kls::pmr::default_resource());
        co_await endpoint.put(std::move(message));
    }

    ValueAsync<> post_shutdown_user_ack(Endpoint &endpoint, Mutex &mutex) {
        MutexLock lk = co_await mutex.scoped_lock_async();
        auto message = Block(0, -2, kls::pmr::default_resource());
        co_await endpoint.put(std::move(message));
    }

    ValueAsync<> handle_shutdown_user(Endpoint &endpoint, int32_t id, Mutex &mutex) {
        if (id == -1) co_await post_shutdown_user_ack(endpoint, mutex);
    }

    class ClientImpl : public ClientEndpoint {
    public:
        explicit ClientImpl(std::unique_ptr<Endpoint> endpoint) :
                m_receive{}, m_endpoint{std::move(endpoint)} {
            m_receive = receive_worker();
        }

        ValueAsync<Response> exec(Request request) override {
            int32_t id{};
            auto receive = get_receive_session_future(id);
            auto memory = kls::pmr::default_resource();
            co_await send_message(id, pack(std::move(request), id, memory));
            co_return unpack_response(co_await receive, memory);
        }

        ValueAsync<> close() override {
            co_await uses(*m_endpoint, [this](Endpoint &ep) -> ValueAsync<> {
                co_await post_shutdown_user(ep, m_mutex);
                co_await std::move(m_receive);
            });
        }
    private:
        Mutex m_mutex{};
        ValueAsync<> m_receive;
        std::unique_ptr<Endpoint> m_endpoint;
        // response sync back
        using StagingTable = std::unordered_map<int32_t, Message>;
        using PromiseTable = std::unordered_map<int32_t, ValueFuture<Message>::PromiseHandle>;
        int32_t m_top_id{0};
        SpinLock m_sync{};
        bool m_is_down{false};
        PromiseTable m_promises{};

        ValueFuture<Message> get_receive_session_future(int32_t &id) {
            static constexpr int32_t mask = std::numeric_limits<int32_t>::max();
            std::lock_guard lk{m_sync};
            if (m_is_down) throw ChannelClosed();
            for (;;) {
                if (m_promises.find(id = (m_top_id++ % mask)) != m_promises.end()) continue;
                return ValueFuture<Message>([this, id](auto promise) { m_promises.insert({id, promise}); });
            }
        };

        ValueAsync<> receive_worker() {
            StagingTable staging{};
            for (;;) {
                auto block = co_await m_endpoint->get();
                const auto id = block.id();
                if (id < 0) {
                    co_await handle_shutdown_user(*m_endpoint, id, m_mutex);
                    fail_all_standing_requests();
                    break;
                }
                process_incoming_message(staging, std::move(block), id);
            }
        }

        void fail_all_standing_requests() {
            PromiseTable final{};
            {
                std::lock_guard lk{m_sync};
                m_is_down = true;
                final = std::move(m_promises);
            }
            for (auto&&[k, v]: final) v->fail(std::make_exception_ptr(ChannelClosed()));
        }

        void process_incoming_message(StagingTable &staging, Block block, int32_t id) {
            auto stage_it = staging.find(id);
            if (stage_it == staging.end()) stage_it = staging.insert_or_assign(id, Message{}).first;
            stage_it->second.blocks[stage_it->second.stage++] = std::move(block);
            if (stage_it->second.stage == 3) {
                release_received_message(id, std::move(stage_it->second));
                staging.erase(stage_it);
            }
        };

        void release_received_message(int32_t id, Message &&message) {
            std::lock_guard lk{m_sync};
            auto promise_it = m_promises.find(id);
            if (m_promises.end() == promise_it) throw InconsistentState{};
            promise_it->second->set(std::move(message));
            m_promises.erase(promise_it);
        }

        ValueAsync<> send_message(int32_t id, Message message) {
            try {
                co_await locked_send_message(*m_endpoint, std::move(message), m_mutex);
            }
            catch (...) {
                std::lock_guard lk{m_sync};
                m_promises.erase(id);
                throw;
            }
        }
    };

    class ServerImpl: public ServerEndpoint {
    public:
        explicit ServerImpl(std::unique_ptr<Endpoint> endpoint) : m_endpoint(std::move(endpoint)) {}

        ValueAsync<> run() override {
            co_await uses(*m_endpoint, [this](Endpoint& ep) -> ValueAsync<> {
                StagingTable staging{};
                for (;;) {
                    auto block = co_await ep.get();
                    const auto id = block.id();
                    if (id < 0) {
                        co_await handle_shutdown_user(ep, id, m_mutex);
                        co_await join_all_standing_requests();
                        break;
                    }
                    process_incoming_message(staging, std::move(block), id);
                }
            });
        }

        ValueAsync<> join_all_standing_requests() {
            PromiseTable final{};
            {
                std::lock_guard lk{m_lock};
                m_is_down = true;
                final = std::move(m_processing);
            }
            for (auto&&[k, v]: final) co_await std::move(v);
        }

        ValueAsync<> close() override {
            {
                std::lock_guard lk{m_lock};
                if (m_is_down) co_return;
            }
            co_await post_shutdown_user(*m_endpoint, m_mutex);
        }
    private:
        Mutex m_mutex{};
        std::unique_ptr<Endpoint> m_endpoint;
        // async handling
        using StagingTable = std::unordered_map<int32_t, Message>;
        using PromiseTable = std::unordered_map<int32_t, ValueAsync<>>;
        SpinLock m_lock{};
        bool m_is_down{false};
        PromiseTable m_processing{};

        void process_incoming_message(StagingTable &staging, Block block, int32_t id) {
            auto stage_it = staging.find(id);
            if (stage_it == staging.end()) stage_it = staging.insert_or_assign(id, Message{}).first;
            stage_it->second.blocks[stage_it->second.stage++] = std::move(block);
            if (stage_it->second.stage == 3) {
                start_request_handle(id, std::move(stage_it->second));
                staging.erase(stage_it);
            }
        }

        void start_request_handle(int32_t id, Message&& msg) {
            std::lock_guard lk{m_lock};
            m_processing.insert({id, handle_request_async(id, std::move(msg))});
        }

        ValueAsync<> handle_request_async(int32_t id, Message msg) {
            co_await Redispatch{};
            auto memory = kls::pmr::default_resource();
            try {
                auto response = pack(co_await m_trivial(unpack_request(std::move(msg), memory), m_data), id, memory);
                co_await locked_send_message(*m_endpoint, std::move(response), m_mutex);
            }
            catch (std::exception &e) { puts(e.what()); }
            catch (...) {}
            std::lock_guard lk{m_lock};
            m_processing.erase(id);
        }
    };
}

namespace kls::phttp {
    const char *ChannelClosed::what() const noexcept {
        return "Channel Closed By Client/Server Request";
    }

    std::unique_ptr<ClientEndpoint> ClientEndpoint::create(std::unique_ptr<Endpoint> ep) {
        return std::make_unique<ClientImpl>(std::move(ep));
    }

    std::unique_ptr<ServerEndpoint> ServerEndpoint::create(std::unique_ptr<Endpoint> ep) {
        return std::make_unique<ServerImpl>(std::move(ep));
    }
}
