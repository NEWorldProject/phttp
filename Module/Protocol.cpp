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
#include "kls/coroutine/Future.h"
#include "kls/coroutine/Operation.h"

using namespace kls::io;
using namespace kls::essential;
using namespace kls::coroutine;

namespace {
    struct Message {
        int stage{0};
        kls::phttp::Block blocks[3];
    };
}

namespace kls::phttp {
    ClientEndpoint::ClientEndpoint(std::unique_ptr<Endpoint> endpoint) : m_down{false}, m_receive{},
                                                                         m_endpoint{std::move(endpoint)} {
        m_receive = [this]() -> ValueAsync<> {
            std::unordered_map<int32_t, Message> staging{};
            while (!m_down) {
                auto block = co_await m_endpoint->get();
                const auto id = block.id();
                auto stage_it = staging.find(id);
                if (stage_it == staging.end()) stage_it = staging.insert_or_assign(id, Message{}).first;
                stage_it->second.blocks[stage_it->second.stage++] = std::move(block);
                if (stage_it->second.stage == 3) {
                    {
                        std::lock_guard lk{m_sync};
                        auto promise_it = m_promises.find(id);
                        if (m_promises.end() != promise_it) throw InconsistentState{};
                        auto promise = static_cast<ValueFuture<Message>::PromiseHandle>(promise_it->second);
                        promise->set(std::move(stage_it->second));
                        m_promises.erase(promise_it);
                    }
                    staging.erase(stage_it);
                }
            }
        }();
    }

    static Message pack(Request r, std::pmr::memory_resource *memory) {
        return Message{.blocks = {r.line.pack(memory), r.headers.pack(memory), std::move(r.body)}};
    }

    static Message pack(Response r, std::pmr::memory_resource *memory) {
        return Message{.blocks = {r.line.pack(memory), r.headers.pack(memory), std::move(r.body)}};
    }

    static Request unpack_request(Message m, std::pmr::memory_resource *memory) {
        return Request{
                .line = RequestLine::unpack(m.blocks[0].content(), memory),
                .headers = Headers::unpack(m.blocks[1].content(), memory),
                .body = std::move(m.blocks[2])
        };
    }

    static Response unpack_response(Message m, std::pmr::memory_resource *memory) {
        return Response{
                .line = ResponseLine::unpack(m.blocks[0].content(), memory),
                .headers = Headers::unpack(m.blocks[1].content(), memory),
                .body = std::move(m.blocks[2])
        };
    }

    static void set_message_id(Message &message, int32_t id) noexcept {
        message.blocks[0].set_id(id), message.blocks[1].set_id(id), message.blocks[2].set_id(id);
    }

    ValueAsync<> locked_send_message(Endpoint &endpoint, Message message, Mutex &mutex) {
        MutexLock lk = co_await mutex.scoped_lock_async();
        co_await endpoint.put(std::move(message.blocks[0]));
        co_await endpoint.put(std::move(message.blocks[1]));
        co_await endpoint.put(std::move(message.blocks[2]));
    }

    ValueAsync<Response> ClientEndpoint::exec(Request &&request) {
        int32_t id{};
        auto memory = std::pmr::get_default_resource();
        auto request_message = pack(std::move(request), memory);
        auto receive = [this, &id]() {
            std::lock_guard lk{m_sync};
            for (;;) {
                if (m_promises.find(id = m_top_id++) == m_promises.end()) continue;
                return ValueFuture<Message>([this, id](auto promise) { m_promises.insert({id, promise}); });
            }
        }();
        set_message_id(request_message, id);
        try { co_await locked_send_message(*m_endpoint, std::move(request_message), m_mutex); }
        catch (...) {
            std::lock_guard lk{m_sync};
            m_promises.erase(id);
            throw;
        }
        co_return unpack_response(co_await receive, memory);
    }

    ValueAsync<> ClientEndpoint::close() {
        m_down = true;
        return awaits(m_endpoint->close(), m_receive);
    }

    ValueAsync<void> ServerEndpoint::run() {
        thread::SpinLock lock{};
        std::unordered_map<int32_t, Message> staging{};
        std::unordered_map<int32_t, ValueAsync<>> processing{};
        auto processor = [&, this](int32_t id, Message msg) -> ValueAsync<void> {
            co_await Redispatch{};
            auto memory = std::pmr::get_default_resource();
            try {
                auto response = pack(co_await m_trivial(unpack_request(std::move(msg), memory), m_data), memory);
                set_message_id(response, id);
                co_await locked_send_message(*m_endpoint, std::move(response), m_mutex);
            }
            catch (std::exception &e) { puts(e.what()); }
            catch (...) {}
            std::lock_guard lk{lock};
            processing.erase(id);
        };
        while (!m_down) {
            auto block = co_await m_endpoint->get();
            const auto id = block.id();
            auto stage_it = staging.find(id);
            if (stage_it == staging.end()) stage_it = staging.insert_or_assign(id, Message{}).first;
            stage_it->second.blocks[stage_it->second.stage++] = std::move(block);
            if (stage_it->second.stage == 3) {
                {
                    std::lock_guard lk{lock};
                    processing.insert({id, processor(id, std::move(stage_it->second))});
                }
                staging.erase(stage_it);
            }
        }
        {
            lock.lock();
            auto final = std::move(processing);
            lock.unlock();
            for (auto&&[k, v]: final) co_await std::move(v);
        }
    }

    ValueAsync<> ServerEndpoint::close() {
        m_down = true;
        co_await m_endpoint->close();
    }
}
